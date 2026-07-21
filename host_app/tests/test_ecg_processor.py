import numpy as np

from slecg_host.ecg.processor import EcgProcessor


def _synthetic_ecg(fs: int = 250, seconds: int = 12, bpm: int = 60):
    times = np.arange(fs * seconds, dtype=np.float64) / fs
    signal = 900.0 * np.sin(2.0 * np.pi * 0.18 * times)  # baseline wander
    signal += 80.0 * np.sin(2.0 * np.pi * 50.0 * times)  # mains residue
    rr = 60.0 / bpm
    for beat in np.arange(1.0, seconds - 0.5, rr):
        signal += 9000.0 * np.exp(-0.5 * ((times - beat) / 0.025) ** 2)
        signal -= 2500.0 * np.exp(-0.5 * ((times - beat - 0.045) / 0.018) ** 2)
    rng = np.random.default_rng(7)
    signal += rng.normal(0.0, 70.0, len(times))
    return times, signal.astype(np.int16)


def test_processor_suppresses_baseline_and_estimates_heart_rate():
    times, raw = _synthetic_ecg()
    result = EcgProcessor().process(times, raw)

    assert len(result.optimized_raw) == len(raw)
    assert 9 <= len(result.r_peaks) <= 12
    assert result.heart_rate_bpm is not None
    assert abs(result.heart_rate_bpm - 60.0) < 2.0
    assert abs(float(np.mean(result.optimized_raw))) < 50.0


def test_short_signal_returns_without_false_heart_rate():
    times = np.arange(100, dtype=np.float64) / 250.0
    result = EcgProcessor().process(times, np.zeros(100, dtype=np.int16))
    assert result.heart_rate_bpm is None
    assert len(result.r_peaks) == 0
