# Accordeur Chromatique avec Assistance Vocale
Cette application Python permet d'accorder des instruments de musique en utilisant le microphone de l'ordinateur avec un retour vocal pour guider l'utilisateur.
## Fonctionnalités

* Détection de fréquence en temps réel
* Identification de notes chromatiques
* Retour vocal indiquant la note détectée et sa justesse
* Coupure automatique du microphone pendant que l'assistance vocale parle
* Architecture orientée objet avec documentation complète compatible Sphinx
* Algorithme de détection stable pour éviter les fausses détections

## Prérequis

Python 3.6 ou supérieur
Les bibliothèques listées dans requirements.txt

## Installation

Clonez ce dépôt ou téléchargez les fichiers source
Installez les dépendances requises :

bashCopypip install -r requirements.txt
## Utilisation
Exécutez le script principal :
bashCopypython tuneAccess.py
L'application démarrera et vous entendrez le message "Assistant d'accordage activé. Jouez une note pour commencer."
Jouez une note sur votre instrument, et l'assistant vocal vous indiquera :

La note détectée (Do, Ré, Mi, etc.)
L'octave (3, 4, 5, etc.)
Si la note est juste, trop haute ou trop basse

## Architecture du code
L'application est organisée en quatre classes principales :

Aud1. ioAnalyzer : Gère la capture audio et la détection de fréquence
2. NoteDetector : Convertit les fréquences en notes musicales et informations d'accordage
3. VoiceAssistant : Fournit le retour vocal et gère la synthèse de parole
4. TunerApp : Classe principale qui coordonne les autres composants

## Personnalisation
Vous pouvez modifier les paramètres suivants :

- Fréquence de référence (La 4 / A4) : par défaut 440 Hz
- Taille du buffer audio et taux d'échantillonnage
- Algorithme de détection de pitch (via aubio)
- Paramètres de la voix (vitesse, volume)
- Sensibilité de détection des changements de note

## Génération de la documentation
Le code est documenté selon les normes Sphinx. Pour générer la documentation HTML :
bashCopy# Installer sphinx
pip install sphinx sphinx_rtd_theme

### Créer la documentation
mkdir docs
cd docs
sphinx-quickstart
### Configurer selon vos préférences et générer la documentation
make html

## Licence
Ce projet est sous licence privée, tous droits réservés à Amine Khettat - copyright (c) 2025.