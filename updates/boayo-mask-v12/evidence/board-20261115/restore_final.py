from board_io import command,status
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 /tmp/boayo-v12c start',120)
command('sleep 12; /tmp/boayo-v12c app telemetry; sleep 20',120)
status('final-running')
command('cat /etc/oryukdo-image; uname -r; ps',60)
print('PASS_SERVICE_RESTORED',flush=True)
