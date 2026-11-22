#!/usr/bin/env python3
"""Standard WAVs must not depend on whether optional NumPy is installed."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'music'))
import render


class MusicRenderTests(unittest.TestCase):
    def test_canonical_phase_at_wrap_boundary(self):
        # 196 / 22050 reaches a wrap after 225 samples. Algebraically equal
        # vector arithmetic rounds differently here and used to flip a sample.
        with patch.object(render, 'np', None):
            synth = render.Synth()
            samples = synth.voice(0, 's', 196, 25.0, 80, 367)
            self.assertEqual(samples[225], -80 * render.GAIN)
            self.assertEqual(samples[226], 80 * render.GAIN)

    @unittest.skipIf(render.np is None, 'optional NumPy is not installed')
    def test_numpy_and_scalar_write_identical_pcm(self):
        numpy = render.np
        sequence = []
        for frame in range(16):
            # Exercise wrap and duty boundaries, silent/restarted voices,
            # frequency changes, noise and alternating 367/368 sample frames.
            voices = [
                ('s', 196, 25.0 if frame < 8 else 12.5, 80),
                ('s', 523 if frame < 10 else 440, 50.0, 0 if frame in (5, 6) else 90),
                ('s', 110, 37.5, 70), ('s', 262, 50.0, 60),
                ('n', 7000, 50.0, 100), ('n', 16000, 50.0, 127),
            ]
            sequence.append((voices, None))
        with tempfile.TemporaryDirectory() as folder:
            outputs = []
            for backend in (None, numpy):
                path = Path(folder) / ('scalar.wav' if backend is None else 'numpy.wav')
                with patch.object(render, 'np', backend), \
                     patch.object(render, 'render_frames', side_effect=lambda *args: iter(sequence)):
                    render.write_wav(str(path), {}, {}, False)
                outputs.append(path.read_bytes())
            self.assertEqual(outputs[0], outputs[1])


if __name__ == '__main__':
    unittest.main()
