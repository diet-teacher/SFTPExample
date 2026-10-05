#pragma once

// winsock2.h는 windows.h보다 먼저 와야 함 (구버전 winsock.h와 충돌 방지)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

#include <string>
#include <vector>

class SFTPManager
{
public:
    SFTPManager() = default;
    ~SFTPManager() { Disconnect(); }

    bool Connect(const std::string& ip, int port, const std::string& id, const std::string& pw);
    void Disconnect();

    bool GetFileList(const std::string& remoteDir, std::vector<std::string>& outNames);
    bool DownloadFile(const std::string& remotePath, const std::string& localPath);
    // 지정 폴더 바로 아래의 이미지들을 다운로드 (하위 폴더 제외).
    bool DownloadImages(const std::string& remoteDir, const std::string& localDir,
                        size_t& downloaded, size_t& failed);
    bool UploadFile(const std::string& localPath, const std::string& remotePath);

    // 마지막 libssh2 오류 메시지 (libssh2_session_last_error + SFTP 상태 코드)
    std::string GetLastErrorMessage() const;

private:
    SOCKET           m_sock = INVALID_SOCKET;
    LIBSSH2_SESSION* m_session = nullptr;
    LIBSSH2_SFTP*    m_sftp = nullptr;
    bool             m_wsaStarted = false;
    bool             m_libssh2Inited = false;
};
