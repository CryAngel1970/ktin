KTin 2.7.14 - TinTin++ 트리거 파일 로딩/저장 및 BOM 수정 안내
=================================================================

수정 목적
---------
1. KTin이 만든 .tin 파일이 UTF-8 BOM(EF BB BF)으로 시작하여 TinTin++가
   다음 오류를 내던 문제를 수정합니다.

   #오류: #READ {ktin_triggers.tin}: 파일 시작 문자가 올바르지 않습니다 '?'.

2. 트리거 편집 창이 KTin의 어두운 ANSI 테마와 무관하게 흰색으로 보이던
   문제를 수정합니다.

3. 기본 ktin_triggers.tin뿐 아니라 사용자가 선택한 다른 .tin 파일도
   불러오고 다른 이름으로 저장할 수 있게 합니다.

4. 현재 편집 파일을 주소록의 접속 스크립트로 명시적으로 지정할 수 있게
   합니다.

핵심 변경
---------
- trigger_editor.cpp의 SaveTriggerModel()은 WriteUtf8NoBomTextFile()을 사용합니다.
- utils.cpp / utils.h에 WriteUtf8NoBomTextFile()이 추가되어 있습니다.
- 저장되는 파일의 첫 바이트는 '#'의 16진수 값인 23입니다.
- 파일에 기존 UTF-8 BOM이 있어도 편집기에서 저장하면 BOM이 제거됩니다.
- 팝업 배경, 정적 글자, 입력칸, 트리, 버튼을 현재 ANSI 테마에 맞춥니다.
- 트리거 편집기 상단에 다음 버튼을 추가했습니다.

  파일 불러오기...
  다른 이름 저장...
  주소록에 현재 파일 지정

기본 파일과 주소록 동작
----------------------
- 주소록으로 접속하지 않은 상태:
  ktin_triggers.tin을 편집합니다.

- 현재 주소록의 스크립트가 비어 있는 상태:
  임의의 triggers\ktin_<호스트>_<ID>.tin 파일을 만들지 않습니다.
  ktin_triggers.tin을 기본 파일로 지정하고 sessions.ini에도 기록합니다.

- 현재 주소록에 기존 .tin 파일이 지정되어 있는 상태:
  해당 파일을 편집기로 엽니다.

- 다른 파일을 편집하려면:
  '파일 불러오기...'를 누릅니다. 이 동작만으로 주소록 설정은 바뀌지 않습니다.

- 불러온 파일을 주소록 접속 시 자동으로 읽게 하려면:
  '주소록에 현재 파일 지정'을 누릅니다.

- 새 파일을 만들려면:
  '다른 이름 저장...'을 누른 뒤 .tin 파일명을 지정합니다.

ktin_c_고유번호의 의미
----------------------
ktin_c_DB7B75B9687E83DE 같은 문자열은 파일명이 아닙니다.
한글 폴더를 TinTin++ Class로 묶기 위한 내부 식별자입니다.

화면 표시 폴더 이름: 기본
TinTin++ 내부 Class: ktin_c_DB7B75B9687E83DE

폴더 이름을 한글로 저장하면서도 TinTin++에서 안전하게 그룹을 관리하기 위해
Class 식별자는 영문과 숫자로 생성합니다.

빌드
----
Visual Studio 개발자 명령 프롬프트에서 소스 폴더로 이동한 뒤:

  build_msvc.bat

WSL에 MinGW-w64가 설치되어 있다면:

  make clean
  make

확인 절차
---------
1. 새 ktin.exe를 실행합니다.
2. 편집 -> 트리거 편집을 엽니다.
3. 폴더와 트리거를 추가합니다.
4. 인식 패턴 예:

   [ENTER]를 입력하면 게임에 들어갑니다. :

5. 실행 명령 예:

   #CR

6. '저장 후 다시 읽기'를 누릅니다.
7. TinTin++ 입력창에서 다음을 입력합니다.

   #act

8. 등록된 ACTIONS 목록에 패턴이 표시되는지 확인합니다.
9. 직접 시험하려면 다음을 입력합니다.

   #showme {[ENTER]를 입력하면 게임에 들어갑니다. :}

10. 트리거 동작 추적은 다음 명령으로 확인합니다.

   #debug {actions} {on}

BOM 확인
--------
PowerShell:

  Format-Hex -Path .\ktin_triggers.tin | Select-Object -First 2

정상 파일 첫 바이트:

  23

잘못된 UTF-8 BOM 파일 첫 세 바이트:

  EF BB BF

WSL 또는 Linux:

  xxd -l 16 ktin_triggers.tin

정상 출력은 00000000 위치에서 23으로 시작합니다.

트리거가 목록에는 있으나 실제 서버 문구에서 동작하지 않을 때
-----------------------------------------------------------
로그인 프롬프트처럼 줄바꿈 없이 나뉘어 들어오는 문자열은 서버 패킷 분할의
영향을 받을 수 있습니다. 먼저 #act에 트리거가 실제 등록되었는지 확인합니다.
등록되어 있다면 TinTin++에서 다음 값을 시험할 수 있습니다.

  #config {packet patch} {0.5}

또는 서버가 GA/EOR 프롬프트를 보내는지 확인합니다.

수정 파일
---------
- trigger_editor.cpp
- utils.cpp
- utils.h
- app_version.h
- README.md
- Changelog_magun.md
- README_TRIGGER_EDITOR_2.7.14.txt

검증 제한
---------
현재 작업 환경에는 Windows SDK/Visual Studio 및 MinGW-w64 Windows 헤더가 없어
실제 ktin.exe 컴파일과 Windows 실행 시험은 수행하지 못했습니다. 소스 구조,
괄호 균형, 파일 변경점, 저장 함수 연결과 패치 적용 결과를 정적 검사했습니다.
