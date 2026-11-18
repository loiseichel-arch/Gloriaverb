# Gloriaverb v0.3

Réverb multi-effet de **Planet Gloria** (AAX pour Pro Tools, plus AU, VST3 et une app autonome pour tester), écrite avec JUCE. Même famille visuelle que Malade et Slapdash : interface dessinée à la main.

100 % algorithmique (pas de convolution). Trois modules en série, **Delay**, **Reverb** et **EQ**, dans l'ordre que tu veux, puis une section **Master**.

## La fenêtre

- **Synoptique en haut** : In → [Delay] → [Reverb] → [EQ] → Master → Out.
  - Un **clic** sur un module affiche son panneau.
  - Le bouton rond **on / off** allume ou éteint le module (fondu de 10 ms, sans clic). Alt + clic : valeur par défaut.
  - **Glisser** un module à gauche ou à droite change l'ordre (6 ordres possibles, en série). Le changement fait un court fondu de la sortie (10 ms + 10 ms).
  - À l'ouverture : Reverb seule allumée.
- **Master en bas**, toujours en fin de chaîne : Duck, Release, Destroy, Output.
- **Alt + clic** (ou double-clic) sur n'importe quel réglage : retour à la valeur par défaut.

## Reverb

- **7 modes** :
  - **Plate 140** : plaque d'acier façon EMT 140, très dense dès l'attaque, brillante.
  - **Plate 250** : façon EMT 250, mouvement façon chorus, queue plus sombre, graves un peu plus pleins.
  - **Hall** : grand espace, premières réflexions étalées, queue chaude.
  - **Room** : petite pièce, premières réflexions fortes.
  - **Non-lin** : réverb à porte façon années 80. **Time = durée de la porte, comptée depuis chaque attaque**, même sur une note tenue ; une nouvelle attaque la relance ; fermeture nette en 5 ms. Sans plafond : au-delà d'environ 1 s, c'est une longue réverb coupée net.
  - **Glorious** : immense, très dense, modulation lente et profonde. Le fader Early Ref devient **Gravity** : +100 = déclin normal ; en descendant, chaque attaque creuse la queue qui **enfle** ensuite.
  - **Spring** : façon ressort du Space Echo RE-201 : sombre, métallique, sans flottement. Trois ressorts (gauche / droite / centre), chacun une boucle avec une cascade de 100 filtres passe-tout « étirés » : comme dans un vrai ressort, les graves arrivent d'abord et les aigus après, donc chaque écho devient un « boing » au lieu d'une répétition nette. Petit préampli qui chauffe un peu. Le fader Early Ref devient **Boiiing!!** : plus de dispersion et des attaques qui tapent plus fort dans les ressorts. Les potards Mod ne font que secouer très légèrement le réservoir.
  - Changer de mode : fondu d'environ 20 ms ; la queue en cours est coupée.
- **Époque** (Vintage / 80s / Clean), indépendante du mode :
  - **Vintage** : bande ~11 kHz, convertisseurs 12 bits avec changement de gain automatique, légère saturation, 16 bits dans la boucle, aigus qui s'éteignent plus vite, modulation aléatoire instable, souffle vers −79 dBFS.
  - **80s** : ~15 kHz, convertisseurs 16 bits, très légère saturation, modulation aléatoire lente, souffle vers −96 dBFS.
  - **Clean** : pleine bande, virgule flottante, modulation sinus, aucun bruit.
- **Faders** : **Time** (0,1 à 60 s, logarithmique, conservé au changement de mode), **Predelay** (0 à 250 ms), **Early Ref / Gravity / Boiiing!!** (trois réglages séparés, chacun garde sa valeur).
- **Mod** : Depth, Rate. Même échelle dans tous les modes : chaque ligne du réseau bouge (jusqu'à 4 ms), et toute la queue reçoit en plus un vibrato qui grandit avec le carré de Depth. 30 % = chorus net, 100 % = mal de mer.
- **Filters** (en bleu) : **Lo Cut**, **Hi Cut** (12 dB/oct, sur la sortie de la réverb), avec un **interrupteur on / off** pour comparer A/B (allumé par défaut, fondu de 10 ms). Indépendants du on / off de l'EQ.
- **Tail** : **Damping** (droite = aigus plus longs / graves plus courts, gauche = l'inverse) et **Width** (0 à 150 %). Width garde **le même volume** à tous les réglages (+3 dB à 0 %, 0 dB à 100 %, −2,1 dB à 150 %) et règle aussi la stéréo de Destroy.
- **Level** : **Mix** et **Solo Wet** (allumé par défaut : 100 % réverb sans bouger le Mix).

## Delay

- **Digital / Tape** : Tape = répétitions qui s'assombrissent, saturent légèrement et flottent un peu.
- **Mono / Wide / Ping-pong** : mêmes répétitions des deux côtés / répétitions de droite un peu plus tardives (×1,08 + 7 ms) / répétitions qui rebondissent gauche ↔ droite.
- **ms / Sync** : Time en millisecondes (1 ms à 2 s) ou en valeur de note (1/32 à 1/1, avec triolets et pointées), calée sur le tempo de l'hôte (120 BPM sans tempo).
- **Feedback** (0 à 100 %, en Tape la saturation garde les répétitions sous contrôle) et **Mix**.

## EQ

- Trois bandes en cloche, chacune **Freq**, **Gain** (±18 dB) et **Q**.
- Courbe : les bandes en rouge (grises quand l'EQ est éteint), le Lo Cut et le Hi Cut de la réverb en pointillé bleu (gris quand les Filters sont éteints). On glisse les points numérotés (fréquence + gain) et les carrés bleus (Lo / Hi Cut). Alt + clic : défaut.

## Master

- **Duck** : ton jeu (le signal d'entrée propre) baisse la réverb, jusqu'à environ −20 dB à fond ; **Release** (20 ms à 3 s) règle sa remontée.
- **Destroy** : dropouts de cassette usée sur tout le son (sec compris), en fin de chaîne, rapides et crachotants : coupure en 1 ms, remontée en 2 ms, souvent par rafales. En montant : plus fréquents (0,5 à environ 25 par seconde), plus profonds, un peu plus longs (3 à 40 ms). Chaque dropout ferme aussi les aigus et ralentit à peine la « bande ». À 0 : contournement exact. Width règle si les deux côtés décrochent ensemble (0 %) ou séparément (150 %).
- **Output** : ±12 dB.

## Réglages à l'ouverture

Reverb seule, Plate 250, Vintage, Time 2 s, Predelay 0 ms, Early Ref 30 %, Gravity +100, Boiiing!! 50 %, Depth 30 %, Rate 0,6 Hz, Filters on (Lo Cut Off, Hi Cut Off), Damping 0, Width 100 %, Mix 30 %, **Solo Wet ON**. Delay : Tape, Wide, ms, 375 ms (1/8 pointée en Sync), Feedback 35 %, Mix 25 %. EQ : 120 Hz / 1,2 kHz / 6 kHz à 0 dB, Q 0,9. Master : Duck Off, Release 300 ms, Destroy 0, Output 0 dB.

## Mesures (hors ligne, `tools/engine_test.cpp`)

- Déclin des graves : à 10 % près de Time × le multiplicateur du mode, de 0,5 à 8 s, pour Plate 140, Plate 250, Hall, Room, Glorious.
- Niveau de la réverb seule (bruit, Time 2 s) : −5 à −2 dB selon le mode ; Non-lin ≈ 0 dB. Stéréo décorrélée (corrélation ≈ 0 ; Spring −0,12).
- Width : volume constant à ±0,1 dB de 0 à 150 %.
- Non-lin : sur une note tenue 1,5 s, la porte réglée sur 0,4 s se ferme à 0,43 s, réglée sur 1 s à 1,03 s ; une nouvelle attaque la relance.
- Glorious : Gravity +100 = la queue baisse de 19 dB ; −100 = elle monte de 16 dB.
- Spring : 300 Hz arrive vers 37 ms, 2,2 kHz vers 45 ms (chirp dans le bon sens) ; le réservoir est environ deux fois moins « écho » que la v0.2 (rapport crête / moyenne de l'énergie 7,3 contre 16,6) ; RT60 ≈ Time.
- Queues de 60 s : stables dans les 7 modes et les 3 époques (100 s testées, modulation à fond).
- Delay : Mono / Wide / Ping-pong conformes ; Tape à 100 % de feedback reste bornée (30 s testées) ; 1/8 pointée à 120 BPM = 375 ms.
- EQ : +6,00 dB mesurés à 1 kHz pour +6 dB demandés.
- Modulation (son de 1 kHz, Rate 0,6 Hz) : à Depth 30 %, environ ±15 à 30 cents selon le mode ; à 100 %, environ ±50 (Glorious, plus lent) à ±130 cents (Room).
- Destroy : à 30 %, environ 6 dropouts par seconde (médiane 9 ms) ; à 100 %, environ 25 par seconde (médiane 15 ms), le son décroche 35 à 45 % du temps.
- Duck à fond : −20 dB pendant le jeu, −11,5 dB 0,2 s après, 0 dB 1,5 s après (Release 300 ms).
- Les 6 ordres × 7 modes, tous les modules allumés : sortie finie et bornée.
- Charge de la réverb : 3,5 à 4,5 % d'un cœur (48 kHz, stéréo).

## Télécharger et installer

Chaque envoi sur GitHub compile le plugin (onglet **Actions**) et fabrique deux installateurs :

- **Mac** : `Gloriaverb-<version>-mac.dmg`. Ouvre-le, double-clique sur « Install Gloriaverb.pkg » et choisis les formats (VST3, Audio Unit, app ; AAX en option). Universel Apple Silicon + Intel, macOS 11 et plus. Non signé par Apple : la première fois, clic droit > Ouvrir, ou Réglages Système > Confidentialité et sécurité > « Ouvrir quand même ».
- **Windows** : `Gloriaverb-<version>-windows-setup.exe` (VST3 et app ; AAX en option), 64 bits. Non signé : « Informations complémentaires » puis « Exécuter quand même ».

Les sessions faites avec la v0.1 s'ouvrent : mêmes réglages de réverb ; les nouveaux réglages prennent leur valeur par défaut (donc Solo Wet reste tel qu'il était enregistré).

## Compiler

Sur Mac, avec Xcode et CMake :

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Sur Windows (Visual Studio) : `cmake -B build -A x64` puis `cmake --build build --config Release`.

Test du son, sans JUCE : `g++ -O2 -std=c++17 -I Source tools/engine_test.cpp -o engine_test && ./engine_test`.

## Polices

**Patrick Hand** et **Caveat** (licence libre SIL OFL, dans `Assets/Fonts/`) sont intégrées au plugin.

## Identité

Constructeur **Planet Gloria**, code constructeur `Pglo`, code plugin `Glvb`, identifiant `com.planetgloria.gloriaverb`.
