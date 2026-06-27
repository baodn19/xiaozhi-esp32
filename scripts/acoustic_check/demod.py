"""
Real-time AFSK demodulator based on the Goertzel algorithm.
"""

import numpy as np
from collections import deque


class TraceGoertzel:
    """Real-time Goertzel algorithm implementation."""

    def __init__(self, freq: float, n: int):
        """
        Initialize the Goertzel algorithm.

        Args:
            freq: Normalized frequency (target frequency / sample rate)
            n: Window size
        """
        self.freq = freq
        self.n = n

        # Precomputed coefficients
        self.k = int(freq * n)
        self.w = 2.0 * np.pi * freq
        self.cw = np.cos(self.w)
        self.sw = np.sin(self.w)
        self.c = 2.0 * self.cw

        # State variables: store the two most recent values
        self.zs = deque([0.0, 0.0], maxlen=2)

    def reset(self):
        """Reset algorithm state."""
        self.zs.clear()
        self.zs.extend([0.0, 0.0])

    def __call__(self, xs):
        """
        Process a sequence of samples.

        Args:
            xs: Sample sequence

        Returns:
            Computed amplitude
        """
        self.reset()
        for x in xs:
            z1, z2 = self.zs[-1], self.zs[-2]
            z0 = x + self.c * z1 - z2
            self.zs.append(float(z0))
        return self.amp

    @property
    def amp(self) -> float:
        """Compute the current amplitude."""
        z1, z2 = self.zs[-1], self.zs[-2]
        ip = self.cw * z1 - z2
        qp = self.sw * z1
        return np.sqrt(ip**2 + qp**2) / (self.n / 2.0)


class PairGoertzel:
    """Dual-frequency Goertzel demodulator."""

    def __init__(self, f_sample: int, f_space: int, f_mark: int,
                 bit_rate: int, win_size: int):
        """
        Initialize the dual-frequency demodulator.

        Args:
            f_sample: Sample rate
            f_space: Space frequency (usually represents 0)
            f_mark: Mark frequency (usually represents 1)
            bit_rate: Bit rate
            win_size: Goertzel window size
        """
        assert f_sample % bit_rate == 0, "Sample rate must be an integer multiple of bit rate"

        self.Fs = f_sample
        self.F0 = f_space
        self.F1 = f_mark
        self.bit_rate = bit_rate
        self.n_per_bit = int(f_sample // bit_rate)

        f1 = f_mark / f_sample
        f0 = f_space / f_sample

        self.g0 = TraceGoertzel(freq=f0, n=win_size)
        self.g1 = TraceGoertzel(freq=f1, n=win_size)

        self.in_buffer = deque(maxlen=win_size)
        self.out_count = 0

        print(f"PairGoertzel initialized: f0={f0:.6f}, f1={f1:.6f}, win_size={win_size}, n_per_bit={self.n_per_bit}")

    def __call__(self, s: float):
        """
        Process a single sample.

        Args:
            s: Sample value

        Returns:
            (amp0, amp1, p1_prob) - space amplitude, mark amplitude, mark probability
        """
        self.in_buffer.append(s)
        self.out_count += 1

        amp0, amp1, p1_prob = 0, 0, None

        if self.out_count >= self.n_per_bit:
            amp0 = self.g0(self.in_buffer)
            amp1 = self.g1(self.in_buffer)
            p1_prob = amp1 / (amp0 + amp1 + 1e-8)
            self.out_count = 0

        return amp0, amp1, p1_prob


class RealTimeAFSKDecoder:
    """Real-time AFSK decoder triggered by a start frame."""

    def __init__(self, f_sample: int = 16000, mark_freq: int = 1800,
                 space_freq: int = 1500, bitrate: int = 100,
                 s_goertzel: int = 9, threshold: float = 0.5):
        """
        Initialize the real-time AFSK decoder.

        Args:
            f_sample: Sample rate
            mark_freq: Mark frequency
            space_freq: Space frequency
            bitrate: Bit rate
            s_goertzel: Goertzel window size factor (win_size = f_sample // mark_freq * s_goertzel)
            threshold: Decision threshold
        """
        self.f_sample = f_sample
        self.mark_freq = mark_freq
        self.space_freq = space_freq
        self.bitrate = bitrate
        self.threshold = threshold

        win_size = int(f_sample / mark_freq * s_goertzel)

        self.demodulator = PairGoertzel(f_sample, space_freq, mark_freq,
                                       bitrate, win_size)

        self.start_bytes = b'\x01\x02'
        self.end_bytes = b'\x03\x04'
        self.start_bits = "".join(format(int(x), '08b') for x in self.start_bytes)
        self.end_bits = "".join(format(int(x), '08b') for x in self.end_bytes)

        self.state = "idle"  # idle / entering

        self.buffer_prelude: deque = deque(maxlen=len(self.start_bits))
        self.indicators = []
        self.signal_bits = ""
        self.text_cache = ""

        self.decoded_messages = []
        self.total_bits_received = 0

        print(f"Decoder initialized: win_size={win_size}")
        print(f"Start frame: {self.start_bits} (from {self.start_bytes.hex()})")
        print(f"End frame: {self.end_bits} (from {self.end_bytes.hex()})")

    def process_audio(self, samples: np.array) -> str:
        """
        Process audio data and return newly decoded text.

        Args:
            samples: Audio samples (16-bit PCM normalized to float)

        Returns:
            Newly decoded text
        """
        new_text = ""
        for sample in samples:
            amp0, amp1, p1_prob = self.demodulator(sample)
            if p1_prob is not None:
                bit = '1' if p1_prob > self.threshold else '0'
                match self.state:
                    case "idle":
                        self.buffer_prelude.append(bit)
                    case "entering":
                        self.buffer_prelude.append(bit)
                        self.signal_bits += bit
                        self.total_bits_received += 1
                    case _:
                        pass
                self.indicators.append(p1_prob)

                if self.state == "idle" and "".join(self.buffer_prelude) == self.start_bits:
                    self.state = "entering"
                    self.text_cache = ""
                    self.signal_bits = ""
                    self.buffer_prelude.clear()
                elif self.state == "entering" and ("".join(self.buffer_prelude) == self.end_bits or len(self.signal_bits) >= 256):
                    self.state = "idle"
                    self.buffer_prelude.clear()

                if len(self.signal_bits) >= 8:
                    text = self._decode_bits_to_text(self.signal_bits)
                    if len(text) > len(self.text_cache):
                        new_text = text[len(self.text_cache) - len(text):]
                        self.text_cache = text
        return new_text

    def _decode_bits_to_text(self, bits: str) -> str:
        """
        Decode a bit string into text.

        Args:
            bits: Bit string

        Returns:
            Decoded text
        """
        if len(bits) < 8:
            return ""

        decoded_text = ""
        byte_count = len(bits) // 8

        for i in range(byte_count):
            byte_bits = bits[i*8:(i+1)*8]
            byte_val = int(byte_bits, 2)

            if 32 <= byte_val <= 126:
                decoded_text += chr(byte_val)
            elif byte_val == 0:
                continue
            else:
                pass

        return decoded_text

    def clear(self):
        """Clear decoder state."""
        self.indicators = []
        self.signal_bits = ""
        self.decoded_messages = []
        self.total_bits_received = 0
        print("Decoder state cleared")

    def get_stats(self) -> dict:
        """Return decoder statistics."""
        return {
            'prelude_bits': "".join(self.buffer_prelude),
            "state": self.state,
            'total_chars': sum(len(msg) for msg in self.text_cache),
            'buffer_bits': len(self.signal_bits),
            'mark_freq': self.mark_freq,
            'space_freq': self.space_freq,
            'bitrate': self.bitrate,
            'threshold': self.threshold,
        }
