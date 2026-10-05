#include "SFTPManager.h"

#include <windows.h>
#include <iostream>
#include <string>

// 기본값이 있는 입력. 엔터만 치면 기본값 사용
static std::string Ask(const std::string& prompt, const std::string& def = "")
{
    std::cout << prompt;
    if (!def.empty()) std::cout << " [" << def << "]";
    std::cout << ": ";
    std::string s;
    std::getline(std::cin, s);
    return s.empty() ? def : s;
}

// 비밀번호 입력 시 화면에 표시되지 않도록 콘솔 에코만 잠깐 끔
static std::string AskPassword(const std::string& prompt)
{
    std::cout << prompt << ": ";
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
    std::string s;
    std::getline(std::cin, s);
    SetConsoleMode(h, mode);
    std::cout << "\n";
    return s;
}

int main()
{
    // 한글 출력이 깨지지 않도록 콘솔을 UTF-8로 (소스 파일도 UTF-8로 저장되어 있어야 함)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "=== libssh2 SFTP 예제 ===\n";
    std::string ip   = Ask("서버 IP");
    int         port = std::stoi(Ask("포트", "22"));
    std::string id   = Ask("ID");
    std::string pw   = AskPassword("PW");

    SFTPManager sftp;

    // 1. 접속 + 로그인
    std::cout << "\n[1] 접속: " << ip << ":" << port << "\n";
    if (!sftp.Connect(ip, port, id, pw)) {
        std::cout << "[1] 접속/로그인 실패\n";
        sftp.Disconnect();
        return 1;
    }
    std::cout << "[1] 로그인 성공\n";

    // 원격 폴더 바로 아래 이미지들을 C:\Temp에 저장.
    std::string remoteDir = Ask("\n이미지를 받을 원격 폴더", ".");
    const std::string localDir = "C:\\Temp";
    size_t downloaded = 0;
    size_t failed = 0;
    std::cout << "\n이미지 다운로드 시작 -> " << localDir << "\n";
    const bool ok = sftp.DownloadImages(remoteDir, localDir, downloaded, failed);
    std::cout << "다운로드 결과: 성공 " << downloaded << "개, 실패 " << failed << "개\n";
    if (ok && downloaded == 0)
        std::cout << "해당 폴더에 다운로드할 이미지 파일이 없습니다.\n";

    sftp.Disconnect();
    std::cout << "연결 종료\n";
    return ok ? 0 : 1;
}
