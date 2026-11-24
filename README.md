# Gloriaverb v0.4

Réverb multi-effet de **Planet Gloria** (AAX pour Pro Tools, plus AU, VST3 et une app autonome pour tester), écrite avec JUCE. Même famille visuelle que Malade et Slapdash : interface dessinée à la main.

Algorithmique, sauf deux modes faits par convolution d'enregistrements de Loïs (Spring et Vintage Chamber). Trois modules en série, **Delay**, **Reverb** et **EQ**, dans l'ordre que tu veux, puis une section **Master**.

## La fenêtre

- **Synoptique en haut** : In → [Delay] → [Reverb] → [EQ] → Master → Out.
  - Un **clic** sur un module affiche son panneau.
  - Le bouton rond **on / off** allume ou éteint le module (fondu de 10 ms, sans clic). Alt + clic : valeur par défaut.
  - **Glisser** un module à gauche ou à droite change l'ordre (6 ordres possibles, en série). Le changement fait un court fondu de la sortie (10 ms + 10 ms).
  - À l'ouverture : Reverb seule allumée.
- **on** en haut à droite : bypass de tout le plugin. **Arrêt net** : fondu de 5 ms, puis les queues de réverb et de delay sont effacées ; on entend le signal propre. La fenêtre s'estompe. En rallumant, tout repart à zéro.
- **Master en bas**, toujours en fin de chaîne : Duck, Release, Destroy, Output.
- **Alt + clic** (ou double-clic) sur n'importe quel réglage : retour à la valeur par défaut.

## Reverb

- **8 modes** :
  - **Plate 140** : plaque d'acier façon EMT 140, très dense dès l'attaque, brillante.
  - **Plate 250** : façon EMT 250, mouvement façon chorus, queue plus sombre, graves un peu plus pleins.
  - **Hall** : grand espace, premières réflexions étalées, queue chaude.
  - **Room** : petite pièce, premières réflexions fortes.
  - **Non-lin** : réverb à porte façon années 80. **Time = durée de la porte, comptée depuis chaque attaque**, même sur une note tenue ; une nouvelle attaque la relance ; fermeture nette en 5 ms. Sans plafond : au-delà d'environ 1 s, c'est une longue réverb coupée net.
  - **Glorious** : immense, très dense, modulation lente et profonde. Le fader Early Ref devient **Gravity** : +100 = déclin normal ; en descendant, chaque attaque creuse la queue qui **enfle** ensuite.
  - **Spring** : le ressort de la Space Echo de Loïs, enregistré (balayage 96 kHz / 24 bits, déconvolué). Mono, la stéréo est faite par décorrélation. Longueur naturelle 2,8 s.
  - **Vintage Chamber** : la chambre préférée de Loïs, enregistrée en stéréo de la même façon. Longueur naturelle 3,3 s.
  - Dans ces deux modes (convolution sans latence) :
    - **Time** raccourcit l'enregistrement, il ne peut pas l'allonger : le fader est bloqué à la longueur naturelle (repère « max », le haut du fader est grisé). Si Time était plus long dans un autre mode, il reste enregistré tel quel et revient quand tu changes de mode.
    - **Damping** : à droite, les graves s'éteignent plus vite ; à gauche, les aigus (coupure vers 700 Hz).
    - Predelay, Mod (vibrato de la queue), Filters, Width, Mix, Solo Wet et l'époque Vintage marchent comme ailleurs. Early Ref est grisé (les enregistrements ont déjà leurs réflexions).
    - Le retard avant le premier son des enregistrements (7 ms pour la chambre, 22 ms pour le ressort) est retiré : Predelay 0 = son immédiat. Les deux sont réglés au même niveau que les autres modes.
  - Changer de mode : fondu d'environ 20 ms ; la queue en cours est coupée.
- **Époque** (Vintage / Clean), indépendante du mode, **sans aucun souffle** :
  - **Vintage** : bande ~11 kHz, convertisseurs 12 bits avec changement de gain automatique, légère saturation, 16 bits dans la boucle, aigus qui s'éteignent plus vite, modulation aléatoire instable.
  - **Clean** : pleine bande, virgule flottante, modulation sinus.
  - Les sessions de la v0.3 en 80s s'ouvrent en Clean.
- **Faders** : **Time** (0,1 à 60 s, logarithmique, conservé au changement de mode), **Predelay** (0 à 250 ms), **Early Ref / Gravity** (deux réglages séparés, chacun garde sa valeur).
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

- **Band 1** : low shelf (**Freq**, **Gain** ±18 dB). **Band 2** : cloche (**Freq**, **Gain**, **Q**). **Band 3** : high shelf (**Freq**, **Gain**). Les shelves ont une pente fixe, sans Q.
- Courbe : les bandes en rouge (grises quand l'EQ est éteint), le Lo Cut et le Hi Cut de la réverb en pointillé bleu (gris quand les Filters sont éteints). On glisse les points numérotés (fréquence + gain) et les carrés bleus (Lo / Hi Cut). Alt + clic : défaut.

## Master

- **Duck** : ton jeu (le signal d'entrée propre) baisse la réverb, jusqu'à environ −20 dB à fond ; **Release** (20 ms à 3 s) règle sa remontée.
- **Destroy** : dropouts de cassette usée sur tout le son (sec compris), en fin de chaîne, rapides et crachotants : coupure en 1 ms, remontée en 2 ms, souvent par rafales. En montant : plus fréquents (0,5 à environ 25 par seconde), plus profonds, un peu plus longs (3 à 40 ms). Chaque dropout ferme aussi les aigus et ralentit à peine la « bande ». À 0 : contournement exact. Width règle si les deux côtés décrochent ensemble (0 %) ou séparément (150 %).
- **Output** : ±12 dB.

## Réglages à l'ouverture

Reverb seule, Plate 250, Vintage, Time 2 s, Predelay 0 ms, Early Ref 30 %, Gravity +100, Depth 30 %, Rate 0,6 Hz, Filters on (Lo Cut Off, Hi Cut Off), Damping 0, Width 100 %, Mix 30 %, **Solo Wet ON**. Delay : Tape, Wide, ms, 375 ms (1/8 pointée en Sync), Feedback 35 %, Mix 25 %. EQ : 120 Hz / 1,2 kHz / 6 kHz à 0 dB, Q 0,9 (bande 2). Master : Duck Off, Release 300 ms, Destroy 0, Output 0 dB. Plugin **on**.

## Mesures (hors ligne, `tools/engine_test.cpp`)

- Déclin des graves : à 10 % près de Time × le multiplicateur du mode, de 0,5 à 8 s, pour Plate 140, Plate 250, Hall, Room, Glorious.
- Niveau de la réverb seule (bruit, Time 2 s) : −6 à −3 dB selon le mode (Spring −4,6, Vintage Chamber −6,0) ; Non-lin ≈ 0 dB. Stéréo décorrélée (corrélation ≈ 0 ; Spring +0,09).
- Width : volume constant à ±0,1 dB de 0 à 150 %.
- Non-lin : sur une note tenue 1,5 s, la porte réglée sur 0,4 s se ferme à 0,43 s, réglée sur 1 s à 1,03 s ; une nouvelle attaque la relance.
- Glorious : Gravity +100 = la queue baisse de 19 dB ; −100 = elle monte de 16 dB.
- Convolution : écart de −131 dB avec une convolution directe (aucune latence : le premier échantillon sort tout de suite). Time sur les enregistrements : Spring −60 dB à 1,97 s pour 2 s, 0,97 s pour 1 s, 0,30 s pour 0,3 s ; Vintage Chamber 1,96 / 0,98 / 0,31 s ; au-delà de la longueur naturelle, l'enregistrement tel quel.
- Silence en entrée : silence en sortie dans les deux époques (plus de souffle).
- Queues de 60 s : stables dans les 8 modes et les 2 époques (100 s testées, modulation à fond).
- Delay : Mono / Wide / Ping-pong conformes ; Tape à 100 % de feedback reste bornée (30 s testées) ; 1/8 pointée à 120 BPM = 375 ms.
- EQ : low shelf +6 dB à 200 Hz → +6,0 dB à 30 Hz, +2,9 dB à 200 Hz ; cloche −6 dB à 2 kHz → −6,0 dB ; high shelf +6 dB à 8 kHz → +2,7 dB à 8 kHz, +6,0 dB à 20 kHz.
- Bypass : 5 ms après l'arrêt, la sortie est exactement le signal d'entrée ; en rallumant, plus rien de l'ancienne queue.
- Modulation (son de 1 kHz, Rate 0,6 Hz) : à Depth 30 %, environ ±15 à 30 cents selon le mode ; à 100 %, environ ±50 (Glorious, plus lent) à ±130 cents (Room).
- Destroy : à 30 %, environ 6 dropouts par seconde (médiane 9 ms) ; à 100 %, environ 25 par seconde (médiane 15 ms), le son décroche 35 à 45 % du temps.
- Duck à fond : −20 dB pendant le jeu, −11,5 dB 0,2 s après, 0 dB 1,5 s après (Release 300 ms).
- Les 6 ordres × 8 modes, tous les modules allumés : sortie finie et bornée.
- Charge de la réverb : 3 à 4,5 % d'un cœur pour les modes algorithmiques, 3,5 à 5,5 % pour Spring et Vintage Chamber (48 kHz, stéréo).

## Télécharger et installer

Chaque envoi sur GitHub compile le plugin (onglet **Actions**) et fabrique deux installateurs :

- **Mac** : `Gloriaverb-<version>-mac.dmg`. Ouvre-le, double-clique sur « Install Gloriaverb.pkg » et choisis les formats (VST3, Audio Unit, app ; AAX en option). Universel Apple Silicon + Intel, macOS 11 et plus. Non signé par Apple : la première fois, clic droit > Ouvrir, ou Réglages Système > Confidentialité et sécurité > « Ouvrir quand même ».
- **Windows** : `Gloriaverb-<version>-windows-setup.exe` (VST3 et app ; AAX en option), 64 bits. Non signé : « Informations complémentaires » puis « Exécuter quand même ».

Les sessions des versions précédentes s'ouvrent : mêmes réglages ; les nouveaux réglages prennent leur valeur par défaut. Une session en 80s s'ouvre en Clean ; en Spring, elle joue le nouveau ressort enregistré.

## Compiler

Sur Mac, avec Xcode et CMake :

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Sur Windows (Visual Studio) : `cmake -B build -A x64` puis `cmake --build build --config Release`.

Test du son, sans JUCE (depuis la racine du dépôt) : `g++ -O2 -std=c++17 -I Source tools/engine_test.cpp -o engine_test && ./engine_test`.

## Impulsions

`Assets/Impulses/spring.f32` (mono) et `chamber.f32` (stéréo) : 96 kHz, float32, canaux à la suite, intégrées au plugin. Elles sont faites par `tools/make_impulses.py` à partir des enregistrements du balayage (déconvolution, retrait du retard initial, fin coupée à −70 dB, même énergie pour tous).

## Polices

**Patrick Hand** et **Caveat** (licence libre SIL OFL, dans `Assets/Fonts/`) sont intégrées au plugin.

## Identité

Constructeur **Planet Gloria**, code constructeur `Pglo`, code plugin `Glvb`, identifiant `com.planetgloria.gloriaverb`.
