minichlink + USBasp-Pico — Windows x64

구성 파일
- minichlink.exe: Windows 64비트 실행 파일
- libusb-1.0.dll: 실행에 필요한 USB 라이브러리 (exe와 같은 폴더에 두세요)

실행
1. ZIP을 폴더에 압축 해제합니다.
2. 해당 폴더에서 PowerShell 또는 명령 프롬프트를 엽니다.
3. 장치 검색: .\minichlink.exe -i
4. 사용 가능한 옵션: .\minichlink.exe -h

USBasp-Pico 사용 전 Windows USB 드라이버를 설정해야 할 수 있습니다.
업로드된 원본 README와 REVIEW.md에 WinUSB와 libusb-win32 안내가 각각 있어 서로 다릅니다.
사용 중인 USBasp-Pico 펌웨어/인터페이스에 맞는 드라이버 안내를 확인하세요.

이 빌드는 Windows x64 대상이며 실제 USBasp-Pico 하드웨어 연결 테스트는 수행하지 않았습니다.
