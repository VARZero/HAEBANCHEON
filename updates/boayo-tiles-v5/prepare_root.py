from pathlib import Path
import sys
root=Path(sys.argv[1])
start=root/'bin/boayo-start'
start.write_text('''#!/bin/sh
export BOAYO_INIT_TRACE="${BOAYO_INIT_TRACE:-1}"
export BOAYO_INIT_TRACE_UART="${BOAYO_INIT_TRACE_UART:-1}"
exec /usr/bin/boayo-native start "$@"
''')
start.chmod(0o755)
