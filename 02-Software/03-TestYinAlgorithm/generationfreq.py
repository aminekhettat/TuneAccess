# ==============================================================================
#  Fichier: generationfreq.py
# ------------------------------------------------------------------------------
#  Auteur: [NGAKO Yannis Phanuel | WAFEU Manuelle]
#  Date de Création: 2025-11-07
#  Dernière Modification: 2025-11-07 
#  Version: 1.0.0
# ------------------------------------------------------------------------------
#  Description:
#  Script Python permettant de générer une onde sinusoïdale pure à une 
#  fréquence spécifiée par l'utilisateur. Le son est joué en continu et peut 
#  être arrêté en tapant 'a' suivi de la touche Entrée.
#  Dépendances : numpy, sounddevice, (scipy pour d'autres types d'ondes)
# ==============================================================================

import numpy as np
import sounddevice as sd
from scipy.signal import chirp


def generer_et_jouer_son(
    frequence_hz = 440, 
    duree_secondes = 1.5, 
    frequence_echantillonnage = 44100 
):
    """
    Génère une onde sinusoïdale pure à une fréquence donnée et la joue en boucle.
    """
    print(f"Génération d'un son à {frequence_hz} Hz pour {duree_secondes} secondes...")

    # 1. Créer la séquence temporelle
    t = np.linspace(
        0, 
        duree_secondes, 
        int(frequence_echantillonnage * duree_secondes), 
        False
    )

    # 2. Calculer l'onde sinusoïdale (le signal audio)
    amplitude = 1.0
    note = amplitude * np.sin(2 * np.pi * frequence_hz * t)

    # 3. Normalisation (optionnel mais bonne pratique)
    audio_data = note.astype(np.float32) / np.max(np.abs(note))
    
    # 4. Jouer le son
    print("Lecture du son...")
    print("Veuillez taper 'a' puis ENTRÉE pour arrêter la lecture...")
    
    sd.play(audio_data, frequence_echantillonnage, loop=True)
    
    try:
        entree = input()
        
        if entree == 'a':
            print("\n'a' détecté. Arrêt du son.")
            sd.stop()
        else:
            print(f"\nSaisie '{entree}' ignorée (n'est pas 'a'). Arrêt du son.")
            sd.stop()
    except Exception as e:
        print(f"\nArrêt d'urgence de la lecture et du programme. {e}")
        sd.stop()



def main():
    # MODIFIEZ CETTE VALEUR POUR CHOISIR VOTRE FRÉQUENCE ! 
    frequence_desiree = 445.35 

    duree = 2.0 

    generer_et_jouer_son(frequence_hz = frequence_desiree, duree_secondes = duree)

if __name__ == "__main__":
    main()
    print("Le programme est terminé.")