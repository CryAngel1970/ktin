KTin 2.7.13 - 폴더/Class 기반 TinTin++ 트리거 편집기 구현 안내
====================================================================

1. 구현 목적
-----------
KTin은 트리거를 자체 실행하지 않고, 주소록에 연결된 TinTin++ .tin 파일을
편집하는 역할만 담당합니다. 실제 문자열 인식과 명령 실행은 TinTin++의
#action 및 #class 기능이 처리합니다.

메뉴 위치:
  편집 -> 트리거 편집(&A)...

2. TinTin++ 로드 순서
--------------------
가. KTin 시작
  process_manager.cpp의 기존 StartTinTinProcess()가 다음과 같이 실행합니다.

    bin\tt++.exe main.tin

  따라서 main.tin이 가장 먼저 읽힙니다.

나. 주소록으로 접속
  address_book.cpp의 ConnectAddressBookEntry()가 주소록의 스크립트 경로를
  #session 명령의 네 번째 인수로 넘깁니다.

    #session {내부세션명} {서버주소} {포트} {주소록스크립트.tin}

  주소록 스크립트는 접속에 성공한 해당 TinTin++ 세션에서 읽힙니다.
  다른 주소록 세션의 전용 트리거가 섞이는 것을 피하기 위해, 접속 전에
  전역 #read로 주입하는 대신 #session의 파일 인수를 사용했습니다.

다. 주소록 세션이 없는 상태
  편집기는 KTin 실행 폴더의 ktin_triggers.tin을 대상으로 합니다.
  이 파일이 이미 있으면 TinTin++ 백엔드가 시작된 직후 ConPTY 입력으로
  다음 명령을 자동 전송합니다.

    #read {ktin_triggers.tin}

  main.tin은 프로세스 시작 인수로 먼저 처리되므로, 이 파일은 main.tin 뒤에
  읽힙니다. 이 전역 파일의 트리거는 TinTin++ 시작 세션에서 정의되므로
  이후 세션에 상속될 수 있습니다. 주소록마다 완전히 분리할 트리거는 반드시
  주소록별 스크립트 파일에 작성하십시오.

3. 주소록 스크립트 자동 생성
---------------------------
현재 주소록으로 접속했거나 접속 예약 중인 상태에서 트리거 편집기를 열었는데
그 주소록의 스크립트 경로가 비어 있으면 다음 형식의 파일을 자동 지정합니다.

  triggers\ktin_<서버주소>_<8자리ID>.tin

예:
  triggers\ktin_mud.example.com_A1B2C3D4.tin

자동 생성 파일명은 TinTin++의 Windows 파일 처리와 로캘 차이를 피하기 위해
ASCII 문자만 사용합니다. 주소록의 한글 이름은 sessions.ini에 그대로 남습니다.
새 경로는 즉시 sessions.ini의 해당 주소록 항목에 저장됩니다.

4. 폴더와 TinTin++ Class 연결
----------------------------
편집 화면의 폴더 이름은 한글을 그대로 지원합니다. TinTin++ 내부 Class 이름은
한글 지원 여부와 무관하게 다음처럼 안전한 ASCII ID를 사용합니다.

  ktin_c_0123456789ABCDEF

한글 표시 이름, 상하위 폴더 관계, 사용 여부, 트리거 이름/패턴/명령/우선순위는
별도 INI 파일이 아니라 같은 .tin 파일의 #NOP 메타데이터에 UTF-8로 저장됩니다.

관리 구역 예:

  #NOP {KTIN_TRIGGER_EDITOR_BEGIN|1}
  #NOP {KTIN_KNOWN_CLASS|0123456789ABCDEF}
  #NOP {KTIN_FOLDER|0123456789ABCDEF||1|<한글이름 UTF-8 HEX>}
  #NOP {KTIN_TRIGGER|...}
  #NOP {KTIN_TRIGGER_RUNTIME_BEGIN}
  #class {ktin_c_0123456789ABCDEF} {kill}
  #class {ktin_c_0123456789ABCDEF} {open}
  #action {인식 패턴} {실행 명령} {5}
  #class {ktin_c_0123456789ABCDEF} {close}
  #NOP {KTIN_TRIGGER_RUNTIME_END}
  #NOP {KTIN_TRIGGER_EDITOR_END}

폴더를 끄면 그 폴더와 모든 하위 폴더의 #action을 생성하지 않습니다.
다시 읽을 때는 과거 KTin Class를 먼저 kill한 뒤 현재 켜진 폴더만 다시 만듭니다.
폴더를 삭제한 경우에도 옛 Class ID를 정리 목록에 남겨 현재 세션에 이전
트리거가 잔류하지 않게 했습니다.

5. 편집기 기능
-------------
- 폴더 추가
- 선택 항목과 같은 단계에 폴더 추가
- 선택 폴더 아래에 하위 폴더 추가
- 선택 폴더에 트리거 추가
- 폴더/트리거 삭제
- 같은 부모 안에서 위/아래 순서 이동
- 폴더/트리거 사용 여부 켜기/끄기
- 트리거 이름, 인식 패턴, 실행 명령, 우선순위(1~9) 편집
- 저장
- 저장 후 TinTin++에서 즉시 다시 읽기
- 저장 전 .bak 백업
- .tmp 파일 작성 후 원자 교체

6. 기존 .tin 파일 보존 방식
--------------------------
KTin은 다음 두 표식 사이만 다시 작성합니다.

  #NOP {KTIN_TRIGGER_EDITOR_BEGIN|1}
  ...
  #NOP {KTIN_TRIGGER_EDITOR_END}

표식 앞뒤에 사용자가 직접 작성한 alias, variable, event, action, 주석 등은
그대로 보존합니다.

중요한 제한:
  기존 파일에 직접 작성되어 있던 #action은 삭제되지 않지만 자동으로 편집기
  트리로 가져오지 않습니다. 편집 화면에는 KTin 관리 구역에서 만든 항목만
  나타납니다. 기존 #action을 GUI에서 관리하려면 편집기에서 새 트리거로
  옮긴 뒤 원래 줄을 직접 정리해야 합니다.

7. 변경 및 추가 파일
--------------------
새 파일:
  trigger_editor.cpp
  trigger_editor.h

수정 파일:
  constants.h          - 메뉴 ID
  status_bar.cpp       - 편집 메뉴 항목
  main.cpp             - TreeView 초기화, 메뉴 연결, 전역 기본 파일 자동 읽기
  address_book.cpp     - #session의 주소록 스크립트 인수 사용
  address_book.h       - SaveAddressBook() 공개
  app_version.h        - 2.7.13
  README.md
  Changelog_magun.md
  help_dialog.cpp

Makefile과 build_msvc.bat은 *.cpp 전체를 빌드하므로 새 trigger_editor.cpp가
자동으로 포함됩니다.

8. 빌드 방법
-----------
WSL MinGW-w64:

  cd /mnt/d/ktin
  make clean
  make -j"$(nproc)"

Windows Visual Studio 2022 개발자 명령 프롬프트:

  cd /d D:\ktin
  build_msvc.bat

9. 확인 순서
-----------
1) KTin 실행
2) 주소록에서 서버 접속
3) 편집 -> 트리거 편집
4) 주소록 스크립트가 비어 있었다면 sessions.ini의 script_N 항목과
   triggers 폴더의 새 .tin 파일 확인
5) 폴더와 트리거 추가
6) 저장 후 다시 읽기
7) 서버가 인식 패턴 문구를 보낼 때 실행 명령 동작 확인
8) 폴더를 끄고 저장 후 다시 읽은 뒤 트리거가 동작하지 않는지 확인
9) 폴더를 다시 켜고 저장 후 다시 읽은 뒤 다시 동작하는지 확인
10) 다른 주소록으로 접속하여 전용 트리거가 섞이지 않는지 확인

10. 이번 작업의 검증 상태
------------------------
- 업로드된 KTin 2.7.12 전체 소스를 기준으로 변경했습니다.
- 변경 파일과 새 파일의 C++ 구문 트리를 검사했습니다.
- 메뉴 ID 중복 및 새 소스의 Makefile 자동 포함을 확인했습니다.
- 원본 대비 의도한 파일만 달라졌는지 확인했습니다.
- 이 작업 환경에는 x86_64-w64-mingw32-g++와 Visual Studio가 없어 실제
  Windows 실행 파일 링크까지는 수행하지 못했습니다. 위 빌드 명령으로 최종
  컴파일한 뒤 9번 확인 순서를 실행해야 합니다.
