#include "SFTPManager.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

// 프로젝트 설정에 넣지 않아도 링크되도록 (설정에 넣었다면 중복이어도 무해)
#pragma comment(lib, "ws2_32.lib")

bool SFTPManager::Connect(const std::string& ip, int port, const std::string& id, const std::string& pw)
{
    // [1] Winsock 초기화. 이 프로세스에서 소켓 API를 쓰겠다고 OS에 알림 (네트워크 통신 없음)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("  WSAStartup 실패\n");
        return false;
    }
    m_wsaStarted = true;

    // libssh2 전역 초기화 (내부 암호화 라이브러리(OpenSSL 등) 초기화. 네트워크 통신 없음)
    if (libssh2_init(0) != 0) {
        std::printf("  libssh2_init 실패\n");
        return false;
    }
    m_libssh2Inited = true;

    // [2] 주소 해석. "192.168.0.10" 같은 IP도, "localhost" 같은 이름도 받기 위해 getaddrinfo 사용
    addrinfo hints = {};
    hints.ai_family = AF_INET;        // 단순화를 위해 IPv4만
    hints.ai_socktype = SOCK_STREAM;  // SSH는 TCP 위에서 동작
    addrinfo* addr = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(ip.c_str(), portStr.c_str(), &hints, &addr) != 0 || addr == nullptr) {
        std::printf("  주소 해석 실패: %s\n", ip.c_str());
        return false;
    }

    // [3] TCP 소켓 생성 + 접속. 여기서 TCP 3-way handshake(SYN/SYN-ACK/ACK)가 일어남
    //     아직 SSH는 시작 전. 포트가 막혀 있으면 여기서 실패함
    m_sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (m_sock == INVALID_SOCKET) {
        std::printf("  socket 실패 (WSA 오류 %d)\n", WSAGetLastError());
        freeaddrinfo(addr);
        return false;
    }
    if (connect(m_sock, addr->ai_addr, (int)addr->ai_addrlen) != 0) {
        std::printf("  TCP connect 실패 (WSA 오류 %d)\n", WSAGetLastError());
        freeaddrinfo(addr);
        return false;
    }
    freeaddrinfo(addr);

    // [4] SSH 세션 객체 생성 (메모리상 객체만 만듦. 통신 없음)
    m_session = libssh2_session_init();
    if (!m_session) {
        std::printf("  libssh2_session_init 실패\n");
        return false;
    }
    // 블로킹 모드: 각 libssh2 호출이 응답을 받을 때까지 기다림 (예제 단순화. 기본값도 블로킹)
    libssh2_session_set_blocking(m_session, 1);

    // [5] SSH 핸드셰이크. 이 한 줄 안에서 다음이 순서대로 일어남
    //     - 버전 문자열 교환 ("SSH-2.0-libssh2_x.x" <-> "SSH-2.0-OpenSSH_x.x")
    //     - KEXINIT: 서로 지원하는 키교환/암호/MAC 알고리즘 목록 교환 후 협상
    //     - 키 교환(예: curve25519 / ECDH): 여기서 세션 암호화 키가 만들어짐
    //     - 서버가 호스트 키로 서명 → 클라이언트가 서명 검증
    //     - NEWKEYS 이후 모든 패킷이 암호화됨
    if (libssh2_session_handshake(m_session, m_sock) != 0) {
        std::printf("  SSH 핸드셰이크 실패: %s\n", GetLastErrorMessage().c_str());
        return false;
    }

    // 서버 호스트 키 지문 출력 (SHA256, 32바이트)
    // 실제 제품이라면 여기서 known_hosts처럼 저장된 지문과 비교해야 중간자 공격을 막을 수 있음.
    // 학습용 예제라 출력만 하고 검증은 생략함
    const char* fp = libssh2_hostkey_hash(m_session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (fp) {
        std::printf("  서버 호스트 키 SHA256: ");
        for (int i = 0; i < 32; ++i) std::printf("%02X", (unsigned char)fp[i]);
        std::printf("\n");
    }

    // [6] 사용자 인증. "ssh-userauth" 서비스 요청 후 password 방식 USERAUTH_REQUEST 전송
    //     비밀번호는 이미 암호화된 채널 안에서 전달되므로 평문으로 노출되지 않음
    //     서버가 USERAUTH_SUCCESS를 돌려주면 0 반환 = 로그인 성공
    if (libssh2_userauth_password(m_session, id.c_str(), pw.c_str()) != 0) {
        std::printf("  비밀번호 인증 실패: %s\n", GetLastErrorMessage().c_str());
        return false;
    }

    // [7] SFTP 서브시스템 시작. 내부적으로
    //     - SSH 채널 열기 (CHANNEL_OPEN "session")
    //     - "subsystem sftp" 요청 → 서버가 sftp-server 프로세스를 붙여줌
    //     - SSH_FXP_INIT / SSH_FXP_VERSION 으로 SFTP 프로토콜 버전(3) 협상
    //     이후 파일 작업은 모두 이 채널 위의 SFTP 패킷으로 오감
    m_sftp = libssh2_sftp_init(m_session);
    if (!m_sftp) {
        std::printf("  SFTP 초기화 실패: %s\n", GetLastErrorMessage().c_str());
        return false;
    }

    return true;
}

void SFTPManager::Disconnect()
{
    // SFTP 채널 닫기 (CHANNEL_EOF / CHANNEL_CLOSE 전송)
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    // SSH_MSG_DISCONNECT 를 보내 서버에 정상 종료를 알리고 세션 메모리 해제
    if (m_session) {
        libssh2_session_disconnect(m_session, "Normal Shutdown");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    // TCP 연결 종료 (FIN)
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    if (m_libssh2Inited) {
        libssh2_exit();
        m_libssh2Inited = false;
    }
    if (m_wsaStarted) {
        WSACleanup();
        m_wsaStarted = false;
    }
}

bool SFTPManager::GetFileList(const std::string& remoteDir, std::vector<std::string>& outNames)
{
    outNames.clear();
    if (!m_sftp) return false;

    // SSH_FXP_OPENDIR 요청 → 서버가 디렉터리 핸들(문자열)을 돌려줌
    LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(m_sftp, remoteDir.c_str());
    if (!dir) return false;

    char name[512];
    char longEntry[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    bool ok = true;
    while (true) {
        // SSH_FXP_READDIR 요청. 서버는 한 번에 여러 항목을 묶어 보내고,
        // libssh2가 그걸 버퍼링해 두었다가 호출마다 하나씩 꺼내줌
        // 반환값: >0 이름 길이, 0 = 더 이상 항목 없음(SSH_FX_EOF), <0 = 오류
        int rc = libssh2_sftp_readdir_ex(dir, name, sizeof(name),
                                         longEntry, sizeof(longEntry), &attrs);
        if (rc < 0) { ok = false; break; }
        if (rc == 0) break;

        std::string entry(name, rc);
        if (entry == "." || entry == "..") continue;
        outNames.push_back(entry);
    }

    // SSH_FXP_CLOSE 로 서버측 디렉터리 핸들 해제
    libssh2_sftp_closedir(dir);
    return ok;
}

bool SFTPManager::DownloadImages(const std::string& remoteDir, const std::string& localDir,
                                size_t& downloaded, size_t& failed)
{
    downloaded = failed = 0;
    if (!m_sftp || remoteDir.empty() || localDir.empty()) return false;

    std::vector<std::string> names;
    if (!GetFileList(remoteDir, names)) {
        std::cout << "목록 조회 실패: " << GetLastErrorMessage() << "\n";
        return false;
    }

    const auto destination = std::filesystem::u8path(localDir);
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        std::cout << "저장 폴더 생성 실패: " << ec.message() << "\n";
        return false;
    }

    for (const auto& name : names) {
        // 원격 이름이 로컬 경로를 벗어나거나 Windows에서 다른 경로로 해석되지 않도록 제한.
        if (name.find_first_of("/\\:<>\"|?*") != std::string::npos ||
            name.empty() || name.back() == '.' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) {
            std::cout << "지원하지 않는 파일명 건너뜀: " << name << "\n";
            continue;
        }
        const auto localPath = destination / std::filesystem::u8path(name);
        std::string extension = localPath.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".jpg" && extension != ".jpeg" && extension != ".png" &&
            extension != ".bmp" && extension != ".gif" && extension != ".tif" &&
            extension != ".tiff" && extension != ".webp") continue;

        const std::string remotePath = remoteDir + (remoteDir.back() == '/' ? "" : "/") + name;
        LIBSSH2_SFTP_ATTRIBUTES attrs = {};
        // lstat으로 일반 파일만 선택 (폴더와 심볼릭 링크 제외).
        if (libssh2_sftp_lstat(m_sftp, remotePath.c_str(), &attrs) != 0) {
            ++failed;
            std::cout << "파일 정보 조회 실패: " << name << " / " << GetLastErrorMessage() << "\n";
            continue;
        }
        if (!(attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ||
            !LIBSSH2_SFTP_S_ISREG(attrs.permissions)) continue;

        if (DownloadFile(remotePath, localPath.u8string())) {
            ++downloaded;
            std::cout << "다운로드 성공: " << localPath.u8string() << "\n";
        } else {
            ++failed;
            std::cout << "다운로드 실패: " << name << " / " << GetLastErrorMessage() << "\n";
        }
    }
    return failed == 0;
}

bool SFTPManager::DownloadFile(const std::string& remotePath, const std::string& localPath)
{
    if (!m_sftp) return false;

    // SSH_FXP_OPEN (읽기 플래그) → 서버가 파일 핸들을 돌려줌
    LIBSSH2_SFTP_HANDLE* file = libssh2_sftp_open(m_sftp, remotePath.c_str(), LIBSSH2_FXF_READ, 0);
    if (!file) return false;

    FILE* fp = nullptr;
    const auto nativePath = std::filesystem::u8path(localPath);
    if (_wfopen_s(&fp, nativePath.c_str(), L"wb") != 0 || !fp) {
        std::printf("  로컬 파일 생성 실패: %s\n", localPath.c_str());
        libssh2_sftp_close(file);
        return false;
    }

    bool ok = true;
    char buf[32 * 1024];
    while (true) {
        // 이 호출이 SSH_FXP_READ(handle, offset, length) 요청으로 변환됨
        // 서버는 SSH_FXP_DATA 로 파일 내용을 돌려줌. offset은 libssh2가 알아서 증가시킴
        // (libssh2는 속도를 위해 여러 READ 요청을 미리 보내두는 파이프라이닝을 함)
        ssize_t n = libssh2_sftp_read(file, buf, sizeof(buf));
        if (n > 0) {
            if (std::fwrite(buf, 1, (size_t)n, fp) != (size_t)n) { ok = false; break; }
        }
        else if (n == 0) {
            break;      // 서버가 SSH_FX_EOF 상태를 보냄 = 파일 끝
        }
        else {
            ok = false; // 읽기 오류
            break;
        }
    }

    if (std::fclose(fp) != 0) ok = false;
    if (libssh2_sftp_close(file) != 0) ok = false;
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(nativePath, ec);
    }
    return ok;
}

bool SFTPManager::UploadFile(const std::string& localPath, const std::string& remotePath)
{
    if (!m_sftp) return false;

    FILE* fp = nullptr;
    if (fopen_s(&fp, localPath.c_str(), "rb") != 0 || !fp) {
        std::printf("  로컬 파일 열기 실패: %s\n", localPath.c_str());
        return false;
    }

    // SSH_FXP_OPEN (쓰기 | 없으면 생성 | 있으면 0바이트로 자름)
    // 마지막 인자는 새로 만들 때의 유닉스 권한 (0644 = rw-r--r--)
    // 임시 파일 후 rename 같은 처리 없이 원격 경로에 바로 씀
    LIBSSH2_SFTP_HANDLE* file = libssh2_sftp_open(
        m_sftp, remotePath.c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!file) {
        std::fclose(fp);
        return false;
    }

    bool ok = true;
    char buf[32 * 1024];
    size_t nread;
    while (ok && (nread = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
        char* p = buf;
        while (nread > 0) {
            // 이 호출이 SSH_FXP_WRITE(handle, offset, data) 요청으로 변환됨
            // 서버는 SSH_FXP_STATUS(OK)로 응답. 한 번에 다 못 보낼 수 있으므로 반복
            ssize_t n = libssh2_sftp_write(file, p, nread);
            if (n < 0) { ok = false; break; }
            p += n;
            nread -= (size_t)n;
        }
    }

    std::fclose(fp);
    libssh2_sftp_close(file);   // SSH_FXP_CLOSE. 서버가 여기서 파일을 닫으며 기록 확정
    return ok;
}

std::string SFTPManager::GetLastErrorMessage() const
{
    if (!m_session) return "(세션 없음)";

    char* msg = nullptr;
    int len = 0;
    // 마지막 오류 코드와 메시지. 마지막 인자 0 = 내부 버퍼를 빌려옴(해제 불필요)
    int code = libssh2_session_last_error(m_session, &msg, &len, 0);

    std::string result = "[" + std::to_string(code) + "] " + (msg ? std::string(msg, len) : "");

    // SFTP 계층 오류면 서버가 보낸 SSH_FXP_STATUS 코드도 함께 표시
    // (2 = NO_SUCH_FILE, 3 = PERMISSION_DENIED, 4 = FAILURE ...)
    if (code == LIBSSH2_ERROR_SFTP_PROTOCOL && m_sftp) {
        unsigned long fx = libssh2_sftp_last_error(m_sftp);
        result += " / SFTP 상태 코드 " + std::to_string(fx);
        switch (fx) {
        case LIBSSH2_FX_NO_SUCH_FILE:      result += " (파일/경로 없음)"; break;
        case LIBSSH2_FX_PERMISSION_DENIED: result += " (권한 없음)"; break;
        case LIBSSH2_FX_FAILURE:           result += " (일반 실패)"; break;
        default: break;
        }
    }
    return result;
}
