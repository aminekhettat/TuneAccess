"""
Application d'accordeur d'instrument avec assistance vocale.

Cette application permet de détecter la fréquence jouée par un instrument de musique
et fournit un retour vocal pour aider à l'accordage chromatique.
"""

import numpy as np
import pyaudio
import aubio
import threading
import time
import pyttsx3
from scipy.signal import find_peaks
import logging

# Configuration du logger
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


class AudioAnalyzer:
    """
    Classe responsable de l'analyse audio en temps réel.
    
    Cette classe capture le son via le microphone et utilise aubio
    pour analyser la fréquence et déterminer la note jouée.
    
    Attributes:
        buffer_size (int): Taille du buffer audio.
        sample_rate (int): Taux d'échantillonnage en Hz.
        channels (int): Nombre de canaux audio.
        pyaudio_instance (pyaudio.PyAudio): Instance de PyAudio.
        stream (pyaudio.Stream): Flux audio.
        pitch_detector (aubio.pitch): Détecteur de pitch d'aubio.
        is_listening (bool): Indique si l'analyseur est en cours d'écoute.
        is_paused (bool): Indique si l'analyseur est en pause (pendant que l'assistance vocale parle).
    """
    
    def __init__(self, buffer_size=1024, sample_rate=44100, hop_size=512):
        """
        Initialise l'analyseur audio.
        
        Args:
            buffer_size (int, optional): Taille du buffer audio. Defaults to 1024.
            sample_rate (int, optional): Taux d'échantillonnage en Hz. Defaults to 44100.
            hop_size (int, optional): Taille de saut pour l'analyse. Defaults to 512.
        """
        self.buffer_size = buffer_size
        self.sample_rate = sample_rate
        self.hop_size = hop_size
        self.channels = 1
        
        self.pyaudio_instance = pyaudio.PyAudio()
        self.pitch_detector = aubio.pitch("yin", buffer_size, hop_size, sample_rate)
        self.pitch_detector.set_unit("Hz")
        self.pitch_detector.set_silence(-40)
        
        self.stream = None
        self.is_listening = False
        self.is_paused = False
        
        # Structure pour stocker les dernières fréquences détectées
        self.recent_frequencies = []
        self.frequency_history_size = 5
        
        logger.info("AudioAnalyzer initialisé avec succès")
    
    def start_listening(self):
        """
        Démarre l'écoute audio depuis le microphone.
        
        Returns:
            bool: True si l'écoute a démarré avec succès, False sinon.
        """
        if self.is_listening:
            logger.warning("L'analyseur écoute déjà")
            return False
        
        try:
            self.stream = self.pyaudio_instance.open(
                format=pyaudio.paFloat32,
                channels=self.channels,
                rate=self.sample_rate,
                input=True,
                frames_per_buffer=self.hop_size
            )
            self.is_listening = True
            logger.info("Écoute audio démarrée")
            return True
        except Exception as e:
            logger.error(f"Erreur lors du démarrage de l'écoute: {str(e)}")
            return False
    
    def stop_listening(self):
        """
        Arrête l'écoute audio.
        
        Returns:
            bool: True si l'arrêt s'est fait avec succès, False sinon.
        """
        if not self.is_listening:
            logger.warning("L'analyseur n'est pas en cours d'écoute")
            return False
        
        try:
            self.stream.stop_stream()
            self.stream.close()
            self.is_listening = False
            logger.info("Écoute audio arrêtée")
            return True
        except Exception as e:
            logger.error(f"Erreur lors de l'arrêt de l'écoute: {str(e)}")
            return False
    
    def pause_listening(self):
        """
        Met en pause l'écoute (pendant que l'assistance vocale parle).
        
        Returns:
            bool: True si la pause a été activée avec succès, False sinon.
        """
        if self.is_paused:
            logger.warning("L'analyseur est déjà en pause")
            return False
        
        self.is_paused = True
        logger.info("Écoute mise en pause")
        return True
    
    def resume_listening(self):
        """
        Reprend l'écoute après une pause.
        
        Returns:
            bool: True si la reprise a réussi, False sinon.
        """
        if not self.is_paused:
            logger.warning("L'analyseur n'est pas en pause")
            return False
        
        self.is_paused = False
        logger.info("Écoute reprise")
        return True
    
    def get_pitch(self):
        """
        Capture et analyse un échantillon audio pour déterminer la fréquence fondamentale.
        
        Returns:
            float: Fréquence détectée en Hz, ou 0 si aucune fréquence n'est détectée.
        """
        if not self.is_listening or self.is_paused:
            return 0
        
        try:
            audiobuffer = self.stream.read(self.hop_size, exception_on_overflow=False)
            signal = np.frombuffer(audiobuffer, dtype=np.float32)
            
            pitch = self.pitch_detector(signal)[0]
            confidence = self.pitch_detector.get_confidence()
            
            # Filtrer les détections de faible confiance
            if confidence < 0.8:
                pitch = 0
            
            # Ajouter à l'historique des fréquences récentes
            if pitch > 0:
                self.recent_frequencies.append(pitch)
                if len(self.recent_frequencies) > self.frequency_history_size:
                    self.recent_frequencies.pop(0)
            
            return pitch
        except Exception as e:
            logger.error(f"Erreur lors de la détection de pitch: {str(e)}")
            return 0
    
    def get_stable_pitch(self):
        """
        Retourne une fréquence stable basée sur l'historique récent.
        
        Returns:
            float: Fréquence moyenne stable, ou 0 si pas assez de données.
        """
        if len(self.recent_frequencies) < 3:
            return 0
        
        # Calculer la moyenne des fréquences récentes
        mean_freq = np.mean(self.recent_frequencies)
        
        # Vérifier si les fréquences sont suffisamment stables
        std_dev = np.std(self.recent_frequencies)
        if std_dev / mean_freq > 0.05:  # Plus de 5% de variation
            return 0
        
        return mean_freq
    
    def cleanup(self):
        """
        Nettoie les ressources utilisées par l'analyseur audio.
        """
        if self.is_listening:
            self.stop_listening()
        
        self.pyaudio_instance.terminate()
        logger.info("Ressources audio libérées")


class NoteDetector:
    """
    Classe responsable de la détection des notes à partir des fréquences.
    
    Cette classe convertit une fréquence en une note musicale et fournit
    des informations sur la justesse de l'accordage.
    
    Attributes:
        reference_a4 (float): Fréquence de référence pour le La 4 (A4) en Hz.
        note_names (list): Liste des noms de notes chromatiques.
    """
    
    def __init__(self, reference_a4=440.0):
        """
        Initialise le détecteur de notes.
        
        Args:
            reference_a4 (float, optional): Fréquence de référence pour A4 en Hz. Defaults to 440.0.
        """
        self.reference_a4 = reference_a4
        self.note_names = ["La", "La#", "Si", "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#"]
        logger.info(f"NoteDetector initialisé avec A4 = {reference_a4} Hz")
    
    def frequency_to_note(self, frequency):
        """
        Convertit une fréquence en une note musicale.
        
        Args:
            frequency (float): Fréquence en Hz.
        
        Returns:
            tuple: (nom de la note, numéro d'octave, différence en cents, fréquence de la note juste)
        """
        if frequency <= 0:
            return None, None, None, None
        
        # Calcul du numéro de demi-ton par rapport à A4
        semitone_number = 12 * np.log2(frequency / self.reference_a4)
        
        # Arrondir au demi-ton le plus proche
        closest_semitone = round(semitone_number)
        
        # Calcul de l'écart en cents (100 cents = 1 demi-ton)
        cents_difference = 100 * (semitone_number - closest_semitone)
        
        # Déterminer la note et l'octave
        note_index = (closest_semitone + 9) % 12  # A = 0, A# = 1, etc.
        octave = 4 + (closest_semitone + 9) // 12
        
        # Calculer la fréquence exacte de la note la plus proche
        exact_frequency = self.reference_a4 * (2 ** (closest_semitone / 12))
        
        return self.note_names[note_index], octave, cents_difference, exact_frequency
    
    def get_tuning_instruction(self, cents_difference):
        """
        Génère des instructions d'accordage basées sur la différence en cents.
        
        Args:
            cents_difference (float): Différence en cents par rapport à la note juste.
        
        Returns:
            str: Instruction d'accordage ("trop bas", "trop haut", ou "juste").
        """
        if abs(cents_difference) < 5:
            return "juste"
        elif cents_difference < 0:
            return "trop bas"
        else:
            return "trop haut"
    
    def get_full_note_info(self, frequency):
        """
        Fournit des informations complètes sur la note détectée.
        
        Args:
            frequency (float): Fréquence en Hz.
        
        Returns:
            dict: Dictionnaire contenant les informations de la note ou None si pas de fréquence valide.
        """
        if frequency <= 0:
            return None
        
        note, octave, cents, exact_freq = self.frequency_to_note(frequency)
        tuning = self.get_tuning_instruction(cents)
        
        return {
            "frequency": frequency,
            "note": note,
            "octave": octave, 
            "cents_difference": cents,
            "tuning_instruction": tuning,
            "exact_frequency": exact_freq
        }


class VoiceAssistant:
    """
    Classe gérant l'assistance vocale pour l'accordage.
    
    Cette classe fournit un retour vocal à l'utilisateur basé sur les notes détectées.
    Elle gère également la pause du microphone pendant qu'elle parle.
    
    Attributes:
        engine (pyttsx3.Engine): Moteur de synthèse vocale.
        is_speaking (bool): Indique si l'assistant vocal est en train de parler.
        audio_analyzer (AudioAnalyzer): Référence à l'analyseur audio pour le contrôle du micro.
    """
    
    def __init__(self, audio_analyzer):
        """
        Initialise l'assistant vocal.
        
        Args:
            audio_analyzer (AudioAnalyzer): Instance de l'analyseur audio.
        """
        self.engine = pyttsx3.init()
        self.engine.setProperty('rate', 150)  # Vitesse de parole
        self.engine.setProperty('volume', 0.9)  # Volume
        
        # Sélection d'une voix française si disponible
        voices = self.engine.getProperty('voices')
        french_voice = None
        
        for voice in voices:
            if 'french' in voice.id.lower() or 'fr' in voice.id.lower():
                french_voice = voice
                break
        
        if french_voice:
            self.engine.setProperty('voice', french_voice.id)
            logger.info(f"Voix française sélectionnée: {french_voice.id}")
        else:
            logger.warning("Pas de voix française trouvée, utilisation de la voix par défaut")
        
        self.is_speaking = False
        self.audio_analyzer = audio_analyzer
        
        # Configuration du callback de fin de parole
        self.engine.connect('finished-utterance', self.on_speech_finished)
        
        logger.info("Assistant vocal initialisé")
    
    def speak(self, text):
        """
        Fait parler l'assistant vocal et met en pause l'analyseur audio.
        
        Args:
            text (str): Texte à prononcer.
        """
        if self.is_speaking:
            logger.warning("L'assistant vocal parle déjà")
            return
        
        # Mettre en pause l'analyseur audio
        self.audio_analyzer.pause_listening()
        self.is_speaking = True
        
        # Démarrer la parole dans un thread séparé
        threading.Thread(target=self._speak_thread, args=(text,), daemon=True).start()
    
    def _speak_thread(self, text):
        """
        Fonction interne pour gérer la parole dans un thread séparé.
        
        Args:
            text (str): Texte à prononcer.
        """
        try:
            logger.info(f"Assistant vocal dit: {text}")
            self.engine.say(text)
            self.engine.runAndWait()
        except Exception as e:
            logger.error(f"Erreur lors de la synthèse vocale: {str(e)}")
        finally:
            self.is_speaking = False
            # Réactiver l'analyseur audio
            self.audio_analyzer.resume_listening()
    
    def on_speech_finished(self):
        """
        Callback appelé lorsque la parole est terminée.
        """
        self.is_speaking = False
        self.audio_analyzer.resume_listening()
    
    def speak_note_info(self, note_info):
        """
        Communique vocalement les informations sur une note détectée.
        
        Args:
            note_info (dict): Informations sur la note détectée.
        """
        if not note_info:
            return
        
        note = note_info["note"]
        octave = note_info["octave"]
        tuning = note_info["tuning_instruction"]
        
        if tuning == "juste":
            message = f"La note {note} {octave} est juste."
        else:
            message = f"La note {note} {octave} est {tuning}."
        
        self.speak(message)


class TunerApp:
    """
    Classe principale de l'application d'accordeur.
    
    Cette classe coordonne les différents composants (analyseur audio,
    détecteur de notes et assistant vocal) et implémente la logique principale.
    
    Attributes:
        audio_analyzer (AudioAnalyzer): Analyseur de son.
        note_detector (NoteDetector): Détecteur de notes.
        voice_assistant (VoiceAssistant): Assistant vocal.
        is_running (bool): Indique si l'application est en cours d'exécution.
    """
    
    def __init__(self, reference_a4=440.0):
        """
        Initialise l'application d'accordeur.
        
        Args:
            reference_a4 (float, optional): Fréquence de référence pour A4. Defaults to 440.0.
        """
        self.audio_analyzer = AudioAnalyzer()
        self.note_detector = NoteDetector(reference_a4=reference_a4)
        self.voice_assistant = VoiceAssistant(self.audio_analyzer)
        
        self.is_running = False
        self.last_spoken_note = None
        self.last_spoken_time = 0
        self.min_time_between_notes = 2.0  # Temps minimal entre deux annonces vocales (secondes)
        
        logger.info("Application d'accordeur initialisée")
    
    def start(self):
        """
        Démarre l'application d'accordeur.
        """
        if self.is_running:
            logger.warning("L'application est déjà en cours d'exécution")
            return
        
        logger.info("Démarrage de l'application d'accordeur")
        self.is_running = True
        self.audio_analyzer.start_listening()
        
        # Message de bienvenue
        self.voice_assistant.speak("Assistant d'accordage activé. Jouez une note pour commencer.")
        
        # Démarrer la boucle principale dans un thread séparé
        threading.Thread(target=self._main_loop, daemon=True).start()
    
    def stop(self):
        """
        Arrête l'application d'accordeur.
        """
        if not self.is_running:
            logger.warning("L'application n'est pas en cours d'exécution")
            return
        
        logger.info("Arrêt de l'application d'accordeur")
        self.is_running = False
        self.audio_analyzer.stop_listening()
        self.voice_assistant.speak("Assistant d'accordage désactivé.")
        self.audio_analyzer.cleanup()
    
    def _main_loop(self):
        """
        Boucle principale de l'application.
        """
        logger.info("Boucle principale démarrée")
        
        while self.is_running:
            try:
                # Obtenir une fréquence stable
                frequency = self.audio_analyzer.get_stable_pitch()
                
                if frequency > 0:
                    # Détecter la note
                    note_info = self.note_detector.get_full_note_info(frequency)
                    
                    # Vérifier si nous devons annoncer cette note
                    current_time = time.time()
                    if note_info and (self.last_spoken_note is None or 
                                      (note_info["note"] != self.last_spoken_note["note"] or 
                                       note_info["octave"] != self.last_spoken_note["octave"] or 
                                       abs(note_info["cents_difference"] - self.last_spoken_note["cents_difference"]) > 10) and
                                      current_time - self.last_spoken_time > self.min_time_between_notes):
                        
                        # Mettre à jour le dernier état annoncé
                        self.last_spoken_note = note_info
                        self.last_spoken_time = current_time
                        
                        # Annoncer la note si l'assistant n'est pas déjà en train de parler
                        if not self.voice_assistant.is_speaking:
                            self.voice_assistant.speak_note_info(note_info)
                
                # Courte pause pour éviter de surcharger le CPU
                time.sleep(0.1)
            
            except Exception as e:
                logger.error(f"Erreur dans la boucle principale: {str(e)}")
                time.sleep(1)
    
    def set_reference_pitch(self, frequency):
        """
        Change la fréquence de référence (A4).
        
        Args:
            frequency (float): Nouvelle fréquence de référence en Hz.
        """
        self.note_detector = NoteDetector(reference_a4=frequency)
        logger.info(f"Fréquence de référence changée à {frequency} Hz")
        self.voice_assistant.speak(f"Fréquence de référence changée à {frequency} Hertz.")


if __name__ == "__main__":
    """Point d'entrée principal de l'application."""
    try:
        # Créer et démarrer l'application
        tuner = TunerApp(reference_a4=440.0)
        tuner.start()
        
        print("Application d'accordeur démarrée. Appuyez sur Ctrl+C pour quitter.")
        
        # Garder le programme principal en cours d'exécution
        while True:
            time.sleep(1)
    
    except KeyboardInterrupt:
        print("\nArrêt de l'application...")
        tuner.stop()
        print("Application arrêtée.")
    
    except Exception as e:
        print(f"Erreur inattendue: {str(e)}")
        if 'tuner' in locals():
            tuner.stop()