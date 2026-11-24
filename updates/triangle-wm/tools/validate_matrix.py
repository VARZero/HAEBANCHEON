"""Same socket and pixel regressions for every numeric/wire combination."""
import json, os, subprocess, sys
from pathlib import Path

binary = sys.argv[1]
root = Path(__file__).resolve().parents[1]
output = root / 'evidence' / 'factorial'
output.mkdir(parents=True, exist_ok=True)
records = []
for numeric in ['float', 'fixed']:
    for wire in ['json', 'binary']:
        for optimized in ['0', '1']:
            label = f'{numeric}-{wire}-opt{optimized}'
            trace = output / (label + '-trace.json')
            env = dict(os.environ, TRIANGLE_NUMERIC=numeric, TRIANGLE_WIRE=wire,
                       TRIANGLE_WIRE_OPT=optimized, TRIANGLE_CHECKPOINT_OPT=optimized,
                       TRIANGLE_MATH_OPT=optimized, TEST_INPUT_VISIBILITY='1',
                       ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1', TRIANGLE_INPUT='1')
            for test in ['test_input.py', 'test_input_mailbox.py']:
                e = dict(env)
                if test == 'test_input.py':
                    e['INPUT_TRACE_PATH'] = str(trace)
                run = subprocess.run([sys.executable, str(root/'tests'/test), binary],
                                     env=e, capture_output=True, text=True, timeout=150)
                (output/(label+'-'+test+'.log')).write_text(run.stdout+run.stderr)
                print(label, test, run.returncode, run.stdout[-500:], flush=True)
                assert run.returncode == 0, run.stderr
                records.append(dict(label=label, test=test, passed=True))
            if optimized == '1':
                previous = output/(f'{numeric}-{wire}-opt0-trace.json')
                assert json.loads(previous.read_text()) == json.loads(trace.read_text()), label
        # The optimized mixed format, coalescing and lifecycle paths.
        for test in ['test_coalescing.py', 'test_boayo.py']:
            env['TRIANGLE_INPUT'] = '0' if test == 'test_boayo.py' else '1'
            run = subprocess.run([sys.executable, str(root/'tests'/test), binary],
                                 env=env, capture_output=True, text=True, timeout=150)
            (output/(label+'-'+test+'.log')).write_text(run.stdout+run.stderr)
            print(label, test, run.returncode, run.stdout[-500:], flush=True)
            assert run.returncode == 0, run.stderr
            records.append(dict(label=label, test=test, passed=True))
(output/'validation.json').write_text(json.dumps(records, indent=2)+'\n')
print('MATRIX_VALIDATION_PASS', flush=True)
