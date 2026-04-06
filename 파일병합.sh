#!/bin/bash

# 출력 파일 이름 설정
OUTPUT_FILE="_파일병합.txt"

# 기존 파일이 있다면 삭제 (새로 만들기 위해)
if [ -f "$OUTPUT_FILE" ]; then
    rm "$OUTPUT_FILE"
fi

echo "코드 병합을 시작합니다..."

# 병합할 확장자 설정 (.h, .cpp, .rc)
# find 명령어를 사용하여 현재 디렉토리의 파일들을 찾습니다.
find . -maxdepth 1 -type f \( -name "*.h" -o -name "*.cpp" -o -name "*.rc" \) | sort | while read -r file; do
    # 파일명 추출 (./ 제거)
    filename=$(basename "$file")
    
    echo "병합 중: $filename"

    # 파일 구분 형식 작성
    echo "--------------------------------------------------" >> "$OUTPUT_FILE"
    echo "$filename" >> "$OUTPUT_FILE"
    echo "--------------------------------------------------" >> "$OUTPUT_FILE"
    
    # 파일 내용 추가
    cat "$file" >> "$OUTPUT_FILE"
    
    # 파일 끝에 빈 줄 추가 (가독성용)
    echo -e "\n" >> "$OUTPUT_FILE"
done

echo "병합 완료! 생성된 파일: $OUTPUT_FILE"