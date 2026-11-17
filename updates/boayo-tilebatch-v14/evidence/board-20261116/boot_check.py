from board_io import command,wait,root
wait(rb'oryukdo# ',0,600)
raw=command('cat /etc/oryukdo-image; uname -r; cat /proc/mounts; ps',90)
(root/'boot-identity.txt').write_text(raw)
assert 'HAEBANCHEON-BoAYO-tilebatch-v14' in raw and '6.12.0-haebancheon-tilebatch14' in raw
assert ' /data ext4 ' in raw
print('PASS_V14_PHYSICAL_LINUX_SHELL_AND_EXT4',flush=True)
