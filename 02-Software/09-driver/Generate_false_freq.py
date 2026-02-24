import numpy as np
import sounddevice as sd

# -----------------------------
# Réglages simples
# -----------------------------
FS = 44100          # fréquence d'échantillonnage
AMP = 0.75          # volume (0.0 à 1.0)
DUREE_NOTE = 1.0    # durée de chaque note (secondes)
PAUSE = 1.0         # pause entre notes (secondes)

def gen_sine(freq_hz, duration_s, fs=FS, amp=AMP):
    t = np.linspace(0, duration_s, int(fs * duration_s), endpoint=False)
    return (amp * np.sin(2 * np.pi * freq_hz * t)).astype(np.float32)

def gen_silence(duration_s, fs=FS):
    return np.zeros(int(fs * duration_s), dtype=np.float32)

def main():
    # Choisis ici les "fausses fréquences" à tester (en Hz)
    # Exemple autour de 220 Hz : 210 (grave), 220 (juste), 230 (aigu)
    freqs = [210, 220, 230]

    audio = []
    for f in freqs:
        audio.append(gen_sine(f, DUREE_NOTE))
        audio.append(gen_silence(PAUSE))  # 1 seconde de pause entre notes

    audio = np.concatenate(audio)

    print("Lecture du test...")
    print("Fréquences jouées:", freqs, "Hz")
    print("Tape 'a' + Entrée pour arrêter.")

    sd.play(audio, FS, loop=True)
    try:
        if input().strip().lower() == "a":
            pass
    finally:
        sd.stop()
        print("Arrêt.")

if __name__ == "__main__":
    main()
