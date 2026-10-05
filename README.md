# libssh2 SFTP 콘솔 예제

원격 서버에 로그인하고 지정 폴더의 이미지들을 `C:\Temp`에 다운로드하는 예제입니다.

## 이미지 일괄 다운로드 사용법

- Visual Studio의 C/C++ → 언어 → C++ 언어 표준을 **ISO C++17 (`/std:c++17`)**로 설정합니다.
- 실행 후 서버 IP, 포트(기본 22), ID, 비밀번호, 원격 이미지 폴더를 입력합니다.
- Linux 서버 경로 예: `/data/images`. Windows OpenSSH 경로 예: `/C:/Users/myuser/Pictures`.
- `C:\Temp`가 없으면 자동 생성하며 원래 파일명으로 저장합니다. 같은 이름의 로컬 파일은 덮어씁니다.
- 지원 확장자: JPG, JPEG, PNG, BMP, GIF, TIF, TIFF, WEBP (대소문자 구분 없음).
- 지정 폴더 바로 아래의 일반 파일만 대상으로 하며, 하위 폴더와 심볼릭 링크는 제외합니다.
- 파일별 성공/실패 및 최종 개수를 출력합니다. 일부 파일이 실패해도 나머지를 계속 받습니다.
- 콘솔 실행 흐름은 일괄 다운로드 후 종료합니다. 기존 단일 다운로드 및 업로드 API는 유지합니다.

아래 기존 실행 예시는 이전 단일 파일 다운로드/업로드 흐름의 참고 자료입니다.

| 파일 | 내용 |
|---|---|
| `SFTPManager.h/.cpp` | Connect / Disconnect / GetFileList / DownloadFile / UploadFile |
| `main.cpp` | 실행 시 IP·포트·ID·PW를 입력받아 1~5단계 수행 |

---

## 1. vcpkg로 libssh2 설치

PowerShell(일반 권한)에서:

```powershell
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd C:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install libssh2:x64-windows
```

- 의존성으로 OpenSSL(암호화 백엔드)과 zlib이 함께 설치됩니다.
- 설치 결과 위치 (`C:\vcpkg\installed\x64-windows\`):
  - `include\libssh2.h`, `include\libssh2_sftp.h`
  - `lib\libssh2.lib` (Release용 import lib), `debug\lib\libssh2.lib` (Debug용)
  - `bin\libssh2.dll`, `bin\libcrypto-3-x64.dll`, `bin\zlib1.dll` (Release용 DLL)
  - `debug\bin\...` (Debug용 DLL. 이름이 `zlibd1.dll`처럼 다를 수 있음)

> Visual Studio에 내장된 vcpkg(`VC\vcpkg`)는 manifest 모드 전용이라 위 classic 명령과 사용법이 다릅니다. 이 예제는 별도로 clone한 vcpkg 기준입니다.

---

## 2. Visual Studio 프로젝트 만들기

1. **새 프로젝트 → 콘솔 앱(C++)** 생성
2. 솔루션 플랫폼을 **x64**로 변경 (vcpkg 트리플렛 `x64-windows`와 맞춰야 함. x86이면 링크 오류)
3. 생성된 기본 `.cpp`를 지우고 `main.cpp`, `SFTPManager.h`, `SFTPManager.cpp`를 프로젝트에 추가

### 방법 A: `vcpkg integrate install` (권장, 가장 간단)

```powershell
C:\vcpkg\vcpkg integrate install
```

한 번만 실행하면 모든 MSBuild 프로젝트에서 다음이 자동으로 됩니다.

- include 경로, lib 경로, `libssh2.lib` 링크 추가
- 빌드 시 필요한 DLL을 출력 폴더(`x64\Debug` 등)로 자동 복사
- Debug/Release 구성에 맞는 lib/DLL 선택

이 경우 아래 **방법 B는 건너뛰고** 3번(공통 설정)만 하면 됩니다.

### 방법 B: 수동 설정 (내부 동작을 보고 싶을 때)

프로젝트 속성 → 구성: **모든 구성**, 플랫폼: **x64**

| 위치 | 값 |
|---|---|
| C/C++ → 일반 → 추가 포함 디렉터리 | `C:\vcpkg\installed\x64-windows\include` |
| 링커 → 일반 → 추가 라이브러리 디렉터리 (Release) | `C:\vcpkg\installed\x64-windows\lib` |
| 링커 → 일반 → 추가 라이브러리 디렉터리 (Debug) | `C:\vcpkg\installed\x64-windows\debug\lib` |
| 링커 → 입력 → 추가 종속성 | `libssh2.lib;ws2_32.lib;` 추가 |

DLL 위치: 실행 파일과 **같은 폴더**(예: `x64\Debug\`)에 복사하거나 PATH에 추가합니다.

```powershell
# Debug 빌드 기준. 프로젝트 폴더에서 실행
copy C:\vcpkg\installed\x64-windows\debug\bin\*.dll .\x64\Debug\
# Release 빌드 기준
copy C:\vcpkg\installed\x64-windows\bin\*.dll .\x64\Release\
```

DLL이 없으면 실행 즉시 "libssh2.dll을 찾을 수 없습니다" 오류가 납니다.
Debug 빌드에 Release DLL을 섞어 써도 대개 돌아가지만, 짝을 맞추는 것이 안전합니다.

### 3. 공통 설정 (A/B 둘 다)

| 위치 | 값 | 이유 |
|---|---|---|
| C/C++ → 명령줄 → 추가 옵션 | `/utf-8` | 소스의 한글 문자열을 UTF-8로 컴파일. `main()`에서 콘솔을 UTF-8로 바꾸므로 이게 없으면 한글 출력이 깨짐 |
| (참고) `ws2_32.lib` | `SFTPManager.cpp`의 `#pragma comment(lib, "ws2_32.lib")`로 자동 링크됨 | 설정에 넣어도 무해 |

빌드 → `Ctrl+F5`로 실행.

---

## 4. 실행 예

```
=== libssh2 SFTP 예제 ===
서버 IP: 127.0.0.1
포트 [22]:
ID: myuser
PW:

[1] 접속: 127.0.0.1:22
  서버 호스트 키 SHA256: 3F9A...
[1] 로그인 성공

원격 폴더 [.]: ./Pictures
[2] 목록 조회 성공 (3개)
    cat.jpg
    ...
다운로드할 원격 이미지 경로 (예: ./Pictures/test.jpg): ./Pictures/cat.jpg
저장할 로컬 경로 (폴더는 미리 있어야 함) [downloaded.jpg]:
[3] 다운로드 성공 -> downloaded.jpg

업로드할 로컬 이미지 경로 [downloaded.jpg]:
원격 저장 경로 [./Pictures/uploaded.jpg]:
[4] 업로드 성공 -> ./Pictures/uploaded.jpg

[5] 연결 종료
```

경로에 한글이 들어가면 입력 인코딩 문제로 실패할 수 있으니 테스트는 영문 경로로 하세요.

---

## 5. 회사 NAS 없이 localhost로 테스트 (Windows OpenSSH Server)

### 5-1. 설치 및 시작

**GUI**: 설정 → 시스템 → 선택적 기능 → 기능 추가(기능 보기) → **OpenSSH 서버** 설치
(Windows 11 Home에서도 제공됩니다)

**또는 관리자 PowerShell**:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service -Name sshd -StartupType Manual   # 테스트용이면 Manual 권장. 상시 켜려면 Automatic
```

설치 시 방화벽 규칙(`OpenSSH-Server-In-TCP`, 22번 포트 인바운드)이 자동 생성됩니다.
localhost 테스트만이라면 방화벽은 상관없습니다.

상태 확인:

```powershell
Get-Service sshd
netstat -an | findstr :22
```

### 5-2. 계정

- **ID**: Windows 사용자 이름 (`whoami` 결과에서 `PC이름\` 뒤 부분, 또는 `$env:USERNAME`)
- **PW**: 그 Windows 계정의 **비밀번호** (PIN은 안 됨)
  - Microsoft 계정으로 로그인하는 PC면 MS 계정 비밀번호를 사용
  - "Windows Hello 로그인만 허용" 설정이 켜져 있으면 비밀번호 인증이 막힐 수 있음. 그 경우 테스트용 로컬 계정을 하나 만드는 게 가장 편합니다:
    ```powershell
    # 관리자 PowerShell
    $p = Read-Host -AsSecureString "새 비밀번호"
    New-LocalUser -Name sftptest -Password $p
    ```
    (새 계정은 첫 SSH 로그인 시 `C:\Users\sftptest` 홈 폴더가 만들어집니다)

### 5-3. 예제 프로그램 없이 먼저 확인

Windows에 기본 포함된 클라이언트로 서버가 정상인지 먼저 봅니다.

```powershell
sftp 사용자이름@localhost
# 처음엔 호스트 키 확인(yes) → 비밀번호 입력 → sftp> 프롬프트
sftp> ls
sftp> exit
```

여기서 안 되면 예제 코드 문제가 아니라 서버/계정 문제입니다.

### 5-4. 예제에서 쓸 값

| 항목 | 값 |
|---|---|
| 서버 IP | `127.0.0.1` (또는 `localhost`) |
| 포트 | `22` |
| 원격 폴더 | `.` (= `C:\Users\사용자이름`) 또는 `/C:/Users/사용자이름/Pictures` |

Windows OpenSSH의 SFTP 경로는 `/C:/...` 처럼 **슬래시 + 드라이브 문자** 형식입니다.
테스트 전에 `C:\Users\사용자이름\Pictures`에 `test.jpg` 하나를 넣어두세요.

### 5-5. 테스트 끝나면

```powershell
Stop-Service sshd
# 완전히 제거하려면 (관리자)
Remove-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
```

---

## 6. 자주 나오는 오류

| 증상 | 원인 |
|---|---|
| `TCP connect 실패 (WSA 오류 10061)` | 서버에 sshd가 안 떠 있음 / 포트 다름 |
| `TCP connect 실패 (WSA 오류 10060)` | 방화벽에 막힘 / IP 틀림 (타임아웃) |
| `SSH 핸드셰이크 실패: [-5] ...` | 키교환 알고리즘 불일치 (매우 오래된 NAS 등) |
| `비밀번호 인증 실패: [-18] ...` | ID/PW 틀림, 또는 서버가 password 방식을 꺼둠 (`sshd_config`의 `PasswordAuthentication`) |
| `SFTP 상태 코드 2 (파일/경로 없음)` | 원격 경로 오타. Windows 서버면 `/C:/...` 형식 확인 |
| `SFTP 상태 코드 3 (권한 없음)` | 해당 폴더 쓰기/읽기 권한 없음 |
| 실행 즉시 DLL 없음 오류 | 2번 DLL 복사 단계 누락 |
| 링크 오류 `LNK2019 libssh2_...` | 플랫폼이 x86이거나 `libssh2.lib` 미추가 |
