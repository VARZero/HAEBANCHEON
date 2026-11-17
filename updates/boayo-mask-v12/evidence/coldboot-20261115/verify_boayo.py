from board_io import command,status
command('sleep 12; /usr/bin/boayo-native app telemetry; sleep 15',120)
s=status('v12-sd-running')
assert s['output']['memory_dma']['errors']==0 and s['output']['memory_dma']['timeouts']==0
assert s['projection']['plan_cache']['revision']=='8f5db34197f157cd8404ab166ed0d9b03742f01656ced910d17d3059a96f9be6'
print('PASS_V12_SD_BOAYO_HARDWARE',flush=True)
