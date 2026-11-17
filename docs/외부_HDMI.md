# 외부 HDMI IP 준비 — 모든 구성 공통

2026-11-17부터 저장소 루트와 SD4를 포함한 모든 구성에서 HDMI IP 본문을 Git에 저장하지 않는다. 공식 Digilent `rgb2dvi`를 고정 upstream commit에서 받아 파일별 SHA-256 확인 후 빌드한다. FPGA의 RTL 동작, 외부 연결 모듈, 핀·클럭 제약은 바뀌지 않았다.

## 준비 명령

저장소 루트에서 실행한다. Python 3과 다운로드 시 인터넷 연결이 필요하다.

```sh
# 현재 실보드 비트스트림의 구성만 준비 (기본값)
python scripts/prepare_current_hdmi.py

# 저장소에 있는 다섯 구성을 모두 준비
python scripts/prepare_current_hdmi.py --all
python scripts/prepare_current_hdmi.py --all --check-only

# 이전 구성 하나만 준비
python scripts/prepare_current_hdmi.py --configuration root
python scripts/prepare_current_hdmi.py --configuration sd4-dma
```

구성 이름은 `root`, `sd4-dma`, `l1-16k-dma3`, `l1-32k-dma3`, `l1-16k-dma3-lru-ram`이다. 기본 구성은 마지막 항목이다. `--destination`으로 외부 다운로드 폴더를 지정할 수도 있지만, 기존 Tcl은 선택한 구성의 `soc/board/vendor/digilent_rgb2dvi/` 경로를 사용한다. 다른 곳에 받을 때에는 검증된 파일을 그 경로로 배치해야 한다.

각 구성의 `upstream_sources.json`을 읽어 11개 파일을 준비한다. 현재 고정 commit은 `0362ce72d39eebc400fdabacff706ee7fa8ad564`다. URL·파일 경로·SHA-256을 확인하며 이미 있는 파일의 해시가 다르면 덮어쓰지 않고 오류로 종료한다. `--check-only`는 네트워크 다운로드나 파일 생성을 하지 않는다. 다운로드된 소스 안의 원 저작권·BSD 고지는 유지한다.

## Git에 남는 것

- 공식 소스 URL·commit·파일 해시 `upstream_sources.json`.
- 직접 작성한 `kfb_hdmi.sv`, 보드 top, 영상 FIFO와 DDR 영상 버퍼 RTL.
- 보드 XDC, MIG 생성 설정, 합성·배치·배선 스크립트.
- 과거 예제의 비교용 해시 목록과 테스트 코드. 예제 HDL 자체는 제공하지 않는다.

Git에서 제외되는 것은 공식 IP의 `src/`·`component.xml` 및 미사용 Puzhi 예제 `.vhd`다. 루트/SD4에서 기존에 추적하던 총 30개 파일도 삭제 커밋으로 제거했다. 로컬 파일은 보존했으며 새 clone에는 내려오지 않는다. 과거 커밋 이력을 강제로 재작성한 것은 아니므로 과거 커밋에는 이전 파일이 남아 있다.

## 빌드·검사와 기존 증거

외부 준비 뒤에는 각 구성의 기존 Vivado 명령을 사용한다. `run_board.py`가 호출하는 `verify_vendor.py`는 공식 IP 11개만 검사하며 미사용 예제 HDL을 요구하지 않는다. 직접 Tcl을 실행할 때에도 필요한 HDMI 파일이 없으면 준비 명령을 안내하고 중단한다. HDMI XSIM 시험도 시작 전에 공식 소스 해시를 확인한다.

```sh
python scripts/verify_sources.py
python scripts/verify_sd4_publication.py
python scripts/verify_current_rtl.py --require-tracked
```

앞의 두 보존 검사에는 해당 구성의 공식 IP 다운로드가 선행돼야 한다. 원래 source/publication manifest와 구현 리포트는 변경하지 않는다. 게시 방식 변경으로 달라진 빌드·검사 도구는 [별도 해시 기록](HDMI_EXTERNALIZATION_HASHES.json)에서 이전/현재 값을 대조한다. 미사용 예제 HDL은 제외했다고 명시하며 검증 통과 파일로 세지 않는다. 현재 비트스트림의 RTL·XDC·MIG 설정은 기존 해시 그대로다.

MIG 생성 RTL/XCI와 XPM은 계속 외부 Vivado 설치본에서 준비한다. 현재 구조와 Vivado 명령은 [현재 비트스트림 안내](현재_비트스트림.md)를 따른다. 이 게시 방식 변경을 새 기능 검증이나 새 타이밍 통과로 해석하지 않는다.
