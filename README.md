# Gloriaverb v0.1 (étape 1)

Réverb de **Planet Gloria** (AAX pour Pro Tools, plus AU, VST3 et une app autonome pour tester), écrite avec JUCE. Même famille visuelle que Malade et Slapdash : interface dessinée à la main.

100 % algorithmique (pas de convolution) : un réseau de 16 lignes de retard rebouclées, avec diffusion d'entrée, déclin en deux bandes et modulation.

## Étape 1 : ce qui est là

- **Modes** : **Plate 140** et **Plate 250**. Hall, Room, Non-lin et Glorious sont visibles mais grisés (étape 2).
  - **Plate 140** : plaque d'acier façon EMT 140, très dense dès l'attaque, brillante.
  - **Plate 250** : façon EMT 250, lignes plus longues, mouvement façon chorus, queue plus sombre et graves un peu plus pleins.
  - Changer de mode fait un fondu de la réverb (environ 20 ms à la sortie, 20 ms au retour) : pas de clic, la queue en cours est coupée.
- **Époque** (Vintage / 80s / Clean), indépendante du mode, ne bouge aucun potard :
  - **Vintage** : bande passante ~11 kHz, convertisseurs 12 bits avec changement de gain automatique (le grain suit le niveau), légère saturation, mots de 16 bits dans la boucle, aigus qui s'éteignent plus vite, modulation aléatoire instable, souffle vers −79 dBFS.
  - **80s** : ~15 kHz, convertisseurs 16 bits, très légère saturation, 16 bits dans la boucle, modulation aléatoire lente (chorus), souffle vers −96 dBFS.
  - **Clean** : pleine bande, calcul en virgule flottante, modulation sinus, aucun bruit.
- **Faders** :
  - **Time** : 0,1 à 60 s pour tous les modes, fader logarithmique (¼ ≈ 0,5 s, milieu ≈ 2,4 s, ¾ ≈ 12 s). Conservé quand on change de mode.
  - **Predelay** : 0 à 250 ms.
  - **Early Ref** : niveau des premières réflexions (taps propres à chaque mode).
- **Mod** : **Depth** et **Rate** (0,05 à 5 Hz).
- **Filters** (en bleu, toujours actifs) : **Lo Cut** (Off à 1 kHz) et **Hi Cut** (1 kHz à Off), 12 dB/oct, sur la sortie de la réverb.
- **Tail** :
  - **Damping** (−100 à +100, 0 = le son naturel du mode) : vers la droite les aigus durent plus longtemps et les graves moins, vers la gauche l'inverse.
  - **Width** : 0 % (mono) à 150 %.
- **Level** : **Mix** (sec / réverb, à puissance constante), **Solo Wet** (100 % réverb sans bouger le Mix), **Output** (±12 dB).
- **Alt + clic** (ou double-clic) sur un réglage : retour à la valeur par défaut.

Réglages à l'ouverture : Plate 250, Vintage, Time 2 s, Predelay 0 ms, Early Ref 30 %, Depth 30 %, Rate 0,6 Hz, Lo Cut Off, Hi Cut Off, Damping 0, Width 100 %, Mix 30 %, Solo Wet OFF, Output 0 dB.

## Mesures (hors ligne, `tools/engine_test.cpp`)

- Temps de déclin dans les graves (< 400 Hz) : à 3 % près de Time de 1 à 12 s ; à 0,3 s la Plate 140 mesure 0,34 s. Plate 250 : Time × 1,12 dans les graves (graves plus pleins voulus).
- Damping (Plate 140, Time 3 s) : −100 → graves 5,8 s / aigus 1,3 s ; 0 → 3,0 / 2,6 s ; +100 → 1,5 / 4,0 s.
- Stéréo : corrélation gauche/droite de la réverb ≈ 0 (large).
- Queues de 60 s : stables (120 s testées dans les trois époques, modulation à fond).
- Charge : environ 2 % d'un cœur (48 kHz, stéréo).

## À venir

- Étape 2 : Hall, Room, Non-lin (Time = durée de la porte), Glorious (Early Ref devient Gravity).
- Étape 3 : section Master avec Duck / Release et Destroy.
- Étape 4 : Delay, EQ, synoptique et routage.

## Télécharger et installer

Chaque envoi sur GitHub compile le plugin (onglet **Actions**) et fabrique deux installateurs :

- **Mac** : `Gloriaverb-<version>-mac.dmg`. Ouvre-le, double-clique sur « Install Gloriaverb.pkg » et choisis les formats (VST3, Audio Unit, app ; AAX en option). Universel Apple Silicon + Intel, macOS 11 et plus. Non signé par Apple : la première fois, clic droit > Ouvrir, ou Réglages Système > Confidentialité et sécurité > « Ouvrir quand même ».
- **Windows** : `Gloriaverb-<version>-windows-setup.exe` (VST3 et app ; AAX en option), 64 bits. Non signé : « Informations complémentaires » puis « Exécuter quand même ».

## Compiler

Sur Mac, avec Xcode et CMake :

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Sur Windows (Visual Studio) : `cmake -B build -A x64` puis `cmake --build build --config Release`.

Test du moteur, sans JUCE : `g++ -O2 -std=c++17 -I Source tools/engine_test.cpp -o engine_test && ./engine_test`.

## Polices

**Patrick Hand** et **Caveat** (licence libre SIL OFL, dans `Assets/Fonts/`) sont intégrées au plugin.

## Identité

Constructeur **Planet Gloria**, code constructeur `Pglo`, code plugin `Glvb`, identifiant `com.planetgloria.gloriaverb`.
