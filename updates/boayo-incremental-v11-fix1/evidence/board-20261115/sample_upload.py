from pathlib import Path
from board_io import command
b=Path('sample_pc').read_bytes()
command(': > /tmp/v11-sample')
for i in range(0,len(b),48):
    octal=''.join('\\%03o'%v for v in b[i:i+48])
    command("printf '"+octal+"' >> /tmp/v11-sample",30)
command('chmod +x /tmp/v11-sample; /tmp/v11-sample',60)
