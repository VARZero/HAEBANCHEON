from pathlib import Path
from board_io import command
b=Path('D:/temp/haebancheon-motion-v7/board-trial-20261114/uart_receive').read_bytes()
command('cp /bin/ddrcheck /tmp/view-rx && : > /tmp/view-rx')
for i in range(0,len(b),48):
    octal=''.join('\\%03o'%v for v in b[i:i+48])
    command("printf '"+octal+"' >> /tmp/view-rx",30)
print('RECEIVER_INSTALLED',flush=True)
