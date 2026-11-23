/* The port's own words in the game's language (tr.h). Each row is an
 * English phrase and the same in Japanese, French, German, Italian and
 * Spanish, as LANGUAGE_JP.. LANGUAGE_ES; NULL keeps the English. The
 * instructions use the game's own names in each language for its moves,
 * Pokémon and minigames, as its messages have them; in them [A] [B] [1]
 * [2] [-] [page] [shake] [either shake] [move] [up] [down] [left] [right]
 * stay as they are (hle_text.c fills them in). Format strings keep their
 * conversions in the same order. */
#include "tr.h"

#include <string.h>

int g_tr_language;

typedef struct {
    const char* en;
    const char* t[5]; /* Japanese, French, German, Italian, Spanish */
} Row;

static const Row kRows[] = {
    /* The Options book's sections */
    {"Display", {"画面", "Affichage", "Anzeige", "Schermo", "Pantalla"}},
    {"Keyboard", {"キーボード", "Clavier", "Tastatur", "Tastiera", "Teclado"}},
    {"Controller", {"コントローラー", "Manette", "Controller", "Controller", "Mando"}},
    {"Gameplay", {"ゲーム", "Jeu", "Spiel", "Gioco", "Juego"}},
    {"Audio", {"サウンド", "Son", "Ton", "Audio", "Sonido"}},
    {"Dual screen", {"2画面", "Double écran", "Zwei Bildschirme", "Doppio schermo", "Doble pantalla"}},
    {"Options", {"オプション", "Options", "Optionen", "Opzioni", "Opciones"}},
    {"PokéPark Pad", {"ポケパークパッド", NULL, NULL, NULL, NULL}},
    {"Friends", {"ともだち", "Amis", "Freunde", "Amici", "Amigos"}},
    {"Map", {"マップ", "Carte", "Karte", "Mappa", "Mapa"}},
    {"Slot 1", {"ファイル 1", "Partie 1", "Spielstand 1", "Partita 1", "Partida 1"}},
    {"Slot 2", {"ファイル 2", "Partie 2", "Spielstand 2", "Partita 2", "Partida 2"}},
    /* Its rows */
    {"Fullscreen", {"フルスクリーン", "Plein écran", "Vollbild", "Schermo intero", "Pantalla completa"}},
    {"Resolution", {"解像度", "Résolution", "Auflösung", "Risoluzione", "Resolución"}},
    {"Aspect ratio", {"画面比率", "Format d'image", "Seitenverhältnis", "Formato", "Relación de aspecto"}},
    {"Anti-aliasing", {"アンチエイリアス", "Anticrénelage", "Kantenglättung", "Anti-aliasing", "Antialiasing"}},
    {"Frame rate", {"フレームレート", "Images/s", "Bildrate", "Frequenza", "Fotogramas"}},
    {"V-sync", {"Vシンク", "Synchro V", "V-Sync", "V-sync", "V-sync"}},
    {"Field of view", {"見える広さ", "Champ de vision", "Sichtfeld", "Campo visivo", "Campo de visión"}},
    {"Filtering", {"フィルタリング", "Filtrage", "Filterung", "Filtro", "Filtrado"}},
    {"Texture pack", {"テクスチャパック", "Pack de textures", "Texturpaket", "Pacchetto texture", "Pack de texturas"}},
    {"Movies", {"ムービー", "Vidéos", "Videos", "Filmati", "Vídeos"}},
    {"Renderer", {"レンダラー", "Rendu", "Renderer", "Renderer", "Renderizador"}},
    {"Updates", {"アップデート", "Mises à jour", "Updates", "Aggiornamenti", "Actualizaciones"}},
    {"Close game", {"ゲームを終わる", "Quitter le jeu", "Spiel beenden", "Chiudi il gioco", "Cerrar el juego"}},
    {"Text speed", {"文字の速さ", "Vitesse du texte", "Textgeschwindigkeit", "Velocità testo", "Velocidad del texto"}},
    {"Berries", {"きのみ", "Baies", "Beeren", "Bacche", "Bayas"}},
    {"Prices", {"ねだん", "Prix", "Preise", "Prezzi", "Precios"}},
    {"Damage dealt", {"あたえるダメージ", "Dégâts infligés", "Verursachter Schaden", "Danni inflitti", "Daño causado"}},
    {"Damage taken", {"受けるダメージ", "Dégâts subis", "Erlittener Schaden", "Danni subiti", "Daño recibido"}},
    {"Chase speed", {"おいかけっこの速さ", "Vitesse de la course", "Fangspiel-Tempo", "Velocità acchiapparello",
                     "Velocidad del pilla pilla"}},
    {"Autosave", {"オートセーブ", "Sauvegarde auto", "Autom. Speichern", "Salvataggio auto", "Autoguardado"}},
    {"Language", {"言語", "Langue", "Sprache", "Lingua", "Idioma"}},
    {"Language pack", {"言語パック", "Pack de langue", "Sprachpaket", "Pacchetto lingua", "Pack de idioma"}},
    {"Swap screens", {"画面を入れかえ", "Inverser les écrans", "Bildschirme tauschen", "Scambia schermi",
                      "Intercambiar pantallas"}},
    {"Map on main", {"マップをメイン画面に", "Carte à l'écran principal", "Karte auf Hauptbildschirm",
                     "Mappa sullo schermo principale", "Mapa en pantalla principal"}},
    {"Music", {"音楽", "Musique", "Musik", "Musica", "Música"}},
    {"Sound effects", {"効果音", "Effets sonores", "Soundeffekte", "Effetti sonori", "Efectos de sonido"}},
    {"Voices", {"ボイス", "Voix", "Stimmen", "Voci", "Voces"}},
    {"Voices language", {"ボイスの言語", "Langue des voix", "Sprache der Stimmen", "Lingua delle voci",
                         "Idioma de las voces"}},
    {"Camera speed", {"カメラの速さ", "Vitesse de la caméra", "Kameratempo", "Velocità telecamera",
                      "Velocidad de la cámara"}},
    {"Invert up/down", {"上下反転", "Inverser haut/bas", "Oben/unten umkehren", "Inverti su/giù",
                        "Invertir arriba/abajo"}},
    {"Invert left/right", {"左右反転", "Inverser gauche/droite", "Links/rechts umkehren", "Inverti sinistra/destra",
                           "Invertir izquierda/derecha"}},
    {"Rumble", {"しんどう", "Vibrations", "Vibration", "Vibrazione", "Vibración"}},
    {"Button icons", {"ボタンの絵", "Icônes des boutons", "Tastensymbole", "Icone dei tasti", "Iconos de botones"}},
    {"Nintendo layout", {"Nintendoの配置", "Disposition Nintendo", "Nintendo-Belegung", "Layout Nintendo",
                         "Distribución Nintendo"}},
    {"Stick dead zone", {"スティックの遊び", "Zone morte du stick", "Stick-Totzone", "Zona morta dello stick",
                         "Zona muerta del stick"}},
    {"Gyro aiming", {"ジャイロでねらう", "Visée gyroscopique", "Gyro-Zielen", "Mira con giroscopio",
                     "Apuntar con giroscopio"}},
    {"Gyro speed", {"ジャイロの速さ", "Vitesse du gyroscope", "Gyro-Tempo", "Velocità giroscopio",
                    "Velocidad del giroscopio"}},
    {"Gyro steering", {"ジャイロでそうさ", "Pilotage gyroscopique", "Gyro-Steuerung", "Sterzo con giroscopio",
                       "Dirección con giroscopio"}},
    {"Reset all controls", {"操作をすべて元にもどす", "Réinitialiser les commandes", "Steuerung zurücksetzen",
                            "Ripristina i comandi", "Restablecer los controles"}},
    {"Import Wii save", {"Wiiのセーブを取りこむ", "Importer une sauvegarde Wii", "Wii-Spielstand importieren",
                         "Importa salvataggio Wii", "Importar partida de Wii"}},
    {"Export Wii save", {"Wiiのセーブを書き出す", "Exporter une sauvegarde Wii", "Wii-Spielstand exportieren",
                         "Esporta salvataggio Wii", "Exportar partida de Wii"}},
    {"Touch controls", {"タッチ操作", "Commandes tactiles", "Touch-Steuerung", "Comandi touch", "Controles táctiles"}},
    /* The bindings */
    {"Up", {"上", "Haut", "Oben", "Su", "Arriba"}},
    {"Down", {"下", "Bas", "Unten", "Giù", "Abajo"}},
    {"Left", {"左", "Gauche", "Links", "Sinistra", "Izquierda"}},
    {"Right", {"右", "Droite", "Rechts", "Destra", "Derecha"}},
    {"2: Jump", {"2: ジャンプ", "2 : Sauter", "2: Springen", "2: Salta", "2: Saltar"}},
    {"1: Dash", {"1: ダッシュ", "1 : Sprint", "1: Sprint", "1: Sprint", "1: Sprint"}},
    {"A: Call", {"A: よぶ", "A : Appeler", "A: Rufen", "A: Chiama", "A: Llamar"}},
    {"B: Look", {"B: 見回す", "B : Regarder", "B: Umsehen", "B: Guarda", "B: Mirar"}},
    {"+: Pad", {"+: パッド", "+ : Pad", "+: Pad", "+: Pad", "+: Pad"}},
    {"-: Photos", {"-: 写真", "- : Photos", "-: Fotos", "-: Foto", "-: Fotos"}},
    {"HOME: First person", {"HOME: ピカチュウの目で見る", "HOME : Vue subjective", "HOME: Ego-Perspektive", "HOME: Prima persona",
                            "HOME: Primera persona"}},
    {"Shake", {"シェイク", "Secouer", "Schütteln", "Scuoti", "Agitar"}},
    {"Shake 2", {"シェイク2", "Secouer 2", "Schütteln 2", "Scuoti 2", "Agitar 2"}},
    {"Menus 2: Select", {"メニュー 2: 決定", "Menus 2 : Valider", "Menüs 2: Auswählen", "Menu 2: Seleziona",
                         "Menús 2: Elegir"}},
    {"Menus 1: Back", {"メニュー 1: もどる", "Menus 1 : Retour", "Menüs 1: Zurück", "Menu 1: Indietro",
                       "Menús 1: Atrás"}},
    {"Menus A", {"メニュー A", "Menus A", "Menüs A", "Menu A", "Menús A"}},
    {"Menus B", {"メニュー B", "Menus B", "Menüs B", "Menu B", "Menús B"}},
    {"Minigames 2", {"ミニゲーム 2", "Mini-jeux 2", "Minispiele 2", "Minigiochi 2", "Minijuegos 2"}},
    {"Minigames 1", {"ミニゲーム 1", "Mini-jeux 1", "Minispiele 1", "Minigiochi 1", "Minijuegos 1"}},
    {"Minigames A", {"ミニゲーム A", "Mini-jeux A", "Minispiele A", "Minigiochi A", "Minijuegos A"}},
    {"Minigames B", {"ミニゲーム B", "Mini-jeux B", "Minispiele B", "Minigiochi B", "Minijuegos B"}},
    {"D-pad up", {"方向ボタン上", "Croix haut", "Steuerkreuz oben", "Croce su", "Cruceta arriba"}},
    {"D-pad down", {"方向ボタン下", "Croix bas", "Steuerkreuz unten", "Croce giù", "Cruceta abajo"}},
    {"D-pad left", {"方向ボタン左", "Croix gauche", "Steuerkreuz links", "Croce sinistra", "Cruceta izquierda"}},
    {"D-pad right", {"方向ボタン右", "Croix droite", "Steuerkreuz rechts", "Croce destra", "Cruceta derecha"}},
    /* Values */
    {"On", {"オン", "Oui", "An", "Sì", "Sí"}},
    {"Off", {"オフ", "Non", "Aus", "No", "No"}},
    {"Auto", {"自動", "Auto", "Auto", "Auto", "Auto"}},
    {"Auto (%dx)", {"自動 (%dx)", NULL, NULL, NULL, NULL}},
    {"Auto (%s)", {"自動 (%s)", NULL, NULL, NULL, NULL}},
    {"On (%d)", {"オン (%d)", "Oui (%d)", "An (%d)", "Sì (%d)", "Sí (%d)"}},
    {" (no gyro)", {" (ジャイロなし)", " (pas de gyroscope)", " (kein Gyro)", " (niente giroscopio)",
                    " (sin giroscopio)"}},
    {" (restart)", {" (再起動)", " (redémarrer)", " (Neustart)", " (riavvia)", " (reiniciar)"}},
    {" (making its pack)", {" (パック作成中)", " (création du pack)", " (Paket wird erstellt)",
                            " (creazione pacchetto)", " (creando el pack)"}},
    {" (pick its disc)", {" (ディスクを選ぶ)", " (choisir le disque)", " (Disc auswählen)", " (scegli il disco)",
                          " (elige el disco)"}},
    {"Original", {"元のまま", "Original", "Original", "Originale", "Original"}},
    {"None found", {"見つかりません", "Aucun trouvé", "Keins gefunden", "Nessuno trovato", "Ninguno"}},
    {"Sharpened", {"くっきり", "Plus net", "Geschärft", "Più nitido", "Más nítido"}},
    {"AI upscaled", {"AIで高画質", "Agrandi par IA", "KI-hochskaliert", "Ingrandito con IA", "Escalado con IA"}},
    {"Light", {"弱", "Léger", "Leicht", "Leggera", "Suave"}},
    {"Normal", {"中", "Normal", "Normal", "Normale", "Normal"}},
    {"Strong", {"強", "Fort", "Stark", "Forte", "Fuerte"}},
    {"Instant", {"すぐに", "Instantané", "Sofort", "Immediato", "Instantáneo"}},
    {"Free", {"無料", "Gratuit", "Gratis", "Gratis", "Gratis"}},
    {"None", {"なし", "Aucun", "Keiner", "Nessuno", "Ninguno"}},
    {"%s is out", {"%sが出ました", "%s est sortie", "%s ist da", "%s è uscita", "Ya salió %s"}},
    {"Checking", {"確認中", "Vérification", "Wird geprüft", "Verifica", "Comprobando"}},
    {"Up to date", {"最新です", "À jour", "Aktuell", "Aggiornato", "Actualizado"}},
    {"Can't check", {"確認できません", "Vérification impossible", "Prüfung nicht möglich", "Impossibile verificare",
                     "No se puede comprobar"}},
    {"From a disc...", {"ディスクから…", "Depuis un disque...", "Von einer Disc ...", "Da un disco...",
                        "Desde un disco..."}},
    {"Restart the game", {"ゲームを再起動してください", "Redémarrez le jeu", "Starte das Spiel neu", "Riavvia il gioco",
                          "Reinicia el juego"}},
    {"Update: %s", {"アップデート: %s", "Mise à jour : %s", "Update: %s", "Aggiornamento: %s", "Actualización: %s"}},
    {"Again to close", {"もう一度で終わる", "Encore pour quitter", "Erneut zum Beenden", "Ancora per chiudere",
                        "Otra vez para cerrar"}},
    {"Press a key", {"キーを　おしてください", "Appuyez sur une touche", "Drücke eine Taste", "Premi un tasto",
                     "Pulsa una tecla"}},
    {"Press a button", {"ボタンを　おしてください", "Appuyez sur un bouton", "Drücke einen Knopf", "Premi un pulsante",
                        "Pulsa un botón"}},
    {"Select: open its download page", {"決定: ダウンロードページを開く", "Valider : ouvrir la page de téléchargement",
                                        "Auswählen: Download-Seite öffnen", "Seleziona: apri la pagina di download",
                                        "Elegir: abrir la página de descarga"}},
    {"Esc: cancel", {"Esc: キャンセル", "Échap : annuler", "Esc: Abbrechen", "Esc: annulla", "Esc: cancelar"}},
    {"Select: rebind    Y: clear", {"決定: へんこう    Y: 消す", "Valider : changer    Y : effacer",
                                    "Auswählen: ändern    Y: löschen", "Seleziona: cambia    Y: cancella",
                                    "Elegir: cambiar    Y: borrar"}},
    {"Select: rebind    Delete: clear", {"決定: へんこう    Delete: 消す", "Valider : changer    Suppr : effacer",
                                         "Auswählen: ändern    Entf: löschen", "Seleziona: cambia    Canc: cancella",
                                         "Elegir: cambiar    Supr: borrar"}},
    /* Languages */
    {"English", {"えいご", "Anglais", "Englisch", "Inglese", "Inglés"}},
    {"Japanese", {"日本語", "Japonais", "Japanisch", "Giapponese", "Japonés"}},
    {"French", {"フランス語", "Français", "Französisch", "Francese", "Francés"}},
    {"German", {"ドイツ語", "Allemand", "Deutsch", "Tedesco", "Alemán"}},
    {"Italian", {"イタリア語", "Italien", "Italienisch", "Italiano", "Italiano"}},
    {"Spanish", {"スペイン語", "Espagnol", "Spanisch", "Spagnolo", "Español"}},
    /* Wii saves (wii_save.c) */
    {"Imported", {"取りこみました", "Importée", "Importiert", "Importato", "Importada"}},
    {"Not a Wii save", {"Wiiのセーブではありません", "Pas une sauvegarde Wii", "Kein Wii-Spielstand",
                        "Non è un salvataggio Wii", "No es una partida de Wii"}},
    {"Another game's save", {"ほかのゲームのセーブです", "Sauvegarde d'un autre jeu", "Spielstand eines anderen Spiels",
                             "Salvataggio di un altro gioco", "Partida de otro juego"}},
    {"Couldn't read it", {"読みこめません", "Lecture impossible", "Konnte nicht gelesen werden", "Impossibile leggerlo",
                          "No se pudo leer"}},
    {"Couldn't save it", {"保存できません", "Enregistrement impossible", "Konnte nicht gespeichert werden",
                          "Impossibile salvarlo", "No se pudo guardar"}},
    /* Language packs (langpack.c) */
    {"Making the %s pack (%d of %d)", {"%sのパックを作成中 (%d/%d)", "Création du pack %s (%d sur %d)",
                                       "%s-Paket wird erstellt (%d von %d)", "Creazione pacchetto %s (%d di %d)",
                                       "Creando el pack de %s (%d de %d)"}},
    {"Making the %s pack", {"%sのパックを作成中", "Création du pack %s", "%s-Paket wird erstellt",
                            "Creazione pacchetto %s", "Creando el pack de %s"}},
    {"%s ready", {"%sの準備ができました", "%s : prêt", "%s bereit", "%s pronto", "%s listo"}},
    {": restart", {": 再起動してください", " : redémarrez", ": Neustart", ": riavvia", ": reinicia"}},
    {" and ", {"と", " et ", " und ", " e ", " y "}},
    {", ", {"、", NULL, NULL, NULL, NULL}},
    {"Reading the disc image", {"ディスクイメージを読みこみ中", "Lecture de l'image disque", "Disc-Abbild wird gelesen",
                                "Lettura dell'immagine del disco", "Leyendo la imagen de disco"}},
    {"The discs' file tables couldn't be read", {"ディスクのファイル一覧を読みこめません",
                                                 "Impossible de lire la liste des fichiers des disques",
                                                 "Die Dateilisten der Discs konnten nicht gelesen werden",
                                                 "Impossibile leggere l'elenco dei file dei dischi",
                                                 "No se pudo leer la lista de archivos de los discos"}},
    {"Nothing on that disc fits this game", {"このディスクには　このゲームに使えるものがありません",
                                             "Rien sur ce disque ne convient à ce jeu",
                                             "Nichts auf dieser Disc passt zu diesem Spiel",
                                             "Niente su quel disco va bene per questo gioco",
                                             "Nada de ese disco sirve para este juego"}},
    {"The pack couldn't be written: is the storage full?", {"パックを書きこめません。空き容量はありますか?",
                                                            "Impossible d'écrire le pack : stockage plein ?",
                                                            "Das Paket konnte nicht geschrieben werden: Speicher voll?",
                                                            "Impossibile scrivere il pacchetto: memoria piena?",
                                                            "No se pudo escribir el pack: ¿almacenamiento lleno?"}},
    {"That disc image couldn't be read. Is the file complete?", {"そのディスクイメージを読みこめません。ファイルは完全ですか?",
                                                                 "Impossible de lire cette image disque. Le fichier est-il complet ?",
                                                                 "Das Disc-Abbild konnte nicht gelesen werden. Ist die Datei vollständig?",
                                                                 "Impossibile leggere l'immagine del disco. Il file è completo?",
                                                                 "No se pudo leer la imagen de disco. ¿Está completo el archivo?"}},
    {"That disc image couldn't be opened", {"そのディスクイメージを開けません", "Impossible d'ouvrir cette image disque",
                                            "Das Disc-Abbild konnte nicht geöffnet werden",
                                            "Impossibile aprire l'immagine del disco", "No se pudo abrir la imagen de disco"}},
    {"The game's own files couldn't be read", {"ゲームのファイルを読みこめません", "Impossible de lire les fichiers du jeu",
                                               "Die Dateien des Spiels konnten nicht gelesen werden",
                                               "Impossibile leggere i file del gioco",
                                               "No se pudieron leer los archivos del juego"}},
    {"The game's own disc image couldn't be opened again", {"ゲームのディスクイメージを開けません",
                                                            "Impossible de rouvrir l'image disque du jeu",
                                                            "Das Disc-Abbild des Spiels ließ sich nicht erneut öffnen",
                                                            "Impossibile riaprire l'immagine del disco del gioco",
                                                            "No se pudo volver a abrir la imagen de disco del juego"}},
    {"The disc image's files couldn't be read", {"ディスクイメージのファイルを読みこめません",
                                                 "Impossible de lire les fichiers de l'image disque",
                                                 "Die Dateien des Disc-Abbilds konnten nicht gelesen werden",
                                                 "Impossibile leggere i file dell'immagine del disco",
                                                 "No se pudieron leer los archivos de la imagen de disco"}},
    {"The languages folder couldn't be found", {"言語フォルダが見つかりません", "Dossier des langues introuvable",
                                                "Sprachordner nicht gefunden", "Cartella delle lingue non trovata",
                                                "No se encontró la carpeta de idiomas"}},
    {"Couldn't start making the pack", {"パックの作成を始められません", "Impossible de commencer le pack",
                                        "Das Paket konnte nicht gestartet werden", "Impossibile avviare il pacchetto",
                                        "No se pudo empezar el pack"}},

    /* The controls' instructions (hle_text.c): the keys' names */
    {"the stick", {"スティック", "le stick", "den Stick", "lo stick", "el stick"}},
    {"nothing", {"なし", "rien", "nichts", "niente", "nada"}},
    {"%s and %s", {"%sと%s", "%s et %s", "%s und %s", "%s e %s", "%s y %s"}},
    {"%s or %s", {"%sか%s", "%s ou %s", "%s oder %s", "%s o %s", "%s o %s"}},
    {"%s, %s, %s and %s", {"%s、%s、%s、%s", "%s, %s, %s et %s", "%s, %s, %s und %s", "%s, %s, %s e %s",
                           "%s, %s, %s y %s"}},
    /* Cake Contraption */
    {"Move the cursor on the screen\nwith the mouse to play.",
     {"画面に　出ている　カーソルを\nマウスで　動かして　操作しよう。", "Déplace le curseur à l'écran\navec la souris pour jouer.",
      "Bewege den Zeiger auf dem\nBildschirm mit der Maus!", "Muovi il cursore sullo schermo\ncon il mouse per giocare.",
      "Mueve el cursor de la pantalla\ncon el ratón para jugar."}},
    {"Move the cursor on the screen\nwith the right stick to play.",
     {"画面に　出ている　カーソルを\n右スティックで　動かして　操作しよう。",
      "Déplace le curseur à l'écran\navec le stick droit pour jouer.",
      "Bewege den Zeiger auf dem\nBildschirm mit dem rechten Stick!",
      "Muovi il cursore sullo schermo\ncon lo stick destro per giocare.",
      "Mueve el cursor de la pantalla\ncon el stick derecho para jugar."}},
    {"Drag on the screen to move\nthe cursor to play.",
     {"画面を　なぞって\nカーソルを　動かそう。", "Fais glisser ton doigt sur l'écran\npour déplacer le curseur.",
      "Wische über den Bildschirm,\num den Zeiger zu bewegen!", "Trascina il dito sullo schermo\nper muovere il cursore.",
      "Desliza el dedo por la pantalla\npara mover el cursor."}},
    {"Aim the cursor at a cake ingredient,\nand press [A] to shoot it!",
     {"ケーキの　材料に\nカーソルを　合わせて\n[A]ボタンで　発射しよう！",
      "Vise les ingrédients des\ngâteaux avec le curseur\net tire en appuyant sur [A] !",
      "Ziele mit dem Zeiger auf eine\nder Backzutaten und drücke [A],\num auf sie zu schießen!",
      "Punta il cursore sugli ingredienti\nper fare le torte e premi [A] per\ncolpirli!",
      "Sitúa el cursor sobre un ingrediente\nde la tarta y dispara pulsando [A]."}},
    /* Dance Inferno */
    {"Press the button on each move's\nicon as it comes up, and hold it\nto its bar's end, to dance\njust like Lopunny.",
     {"おどりの　アイコンが　出たら\nその　ボタンを　おして\nバーの　終わりまで　おしつづけ\nミミロップの　お手本どおりに　おどろう。",
      "Quand une icône apparaît, appuie\nsur la touche indiquée et maintiens-la\njusqu'au bout de la barre pour\nreproduire les mouvements de Lockpin.",
      "Drücke die Taste auf dem Symbol\njeder Bewegung, sobald es erscheint,\nund halte sie bis zum Ende des\nBalkens, um wie Schlapor zu tanzen!",
      "Quando compare l'icona di una mossa,\npremi il tasto indicato e tienilo\npremuto fino alla fine della barra\nper ballare proprio come Lopunny.",
      "Cuando aparezca el icono de un paso,\npulsa el botón que muestra y\nmantenlo hasta el final de la barra\npara bailar igual que Lopunny."}},
    /* Power Bash */
    {"When a Pokémon appears,\npress [shake] by turns\nto swing at it!",
     {"ポケモンが　現れたら\n[shake]を　かわりばんこに　おして\nふっとばそう！",
      "Quand un Pokémon apparaît, appuie\ntour à tour sur [shake]\npour le frapper !",
      "Erscheint ein gegnerisches Pokémon,\ndrücke abwechselnd [shake],\num es wegzuschleudern.",
      "Quando compare un Pokémon, premi\na turno [shake]\nper colpirlo!",
      "¡Cuando aparezca un Pokémon,\npulsa [shake] por turnos\npara golpearlo!"}},
    {"Swing when the Pokémon is close to\nthe center to get a high score.",
     {"真ん中に　近いときに\nふると　得点が　高く　なるよ。",
      "Si tu le frappes quand il est près\ndu centre, il te donnera\nplus de points.",
      "Schlägst du zu, während das Pokémon\nnahe der Mitte ist, gibt es\nmehr Punkte.",
      "Totalizzi più punti se lo colpisci\nproprio quando è vicino al centro.",
      "Cuanto más cerca esté del centro\ncuando lo golpees, más\npuntos conseguirás."}},
    /* Flight of Fancy */
    {"When you want to fly to the side,\npress [left] or [right].",
     {"横に　飛びたいときは\n[left]か　[right]を　おそう。", "Pour te déplacer horizontalement,\nappuie sur [left] ou [right].",
      "Drücke [left] oder [right], um dich\nhorizontal über den Bildschirm\nzu bewegen.",
      "Se vuoi virare a destra o a sinistra,\npremi [left] o [right].",
      "Cuando quieras volar hacia los lados,\npulsa [left] o [right]."}},
    {"When you want to fly to the side,\npush the left stick left or right.",
     {"横に　飛びたいときは\n左スティックを　左右に　たおそう。",
      "Pour te déplacer horizontalement,\nincline le stick gauche\nvers la gauche ou la droite.",
      "Neige den linken Stick nach links\noder rechts, um dich horizontal\nüber den Bildschirm zu bewegen.",
      "Se vuoi virare a destra o a sinistra,\ninclina lo stick sinistro\nin quella direzione.",
      "Cuando quieras volar hacia los lados,\ninclina el stick izquierdo\na la izquierda o a la derecha."}},
    {"When you want to fly to the side,\npush the stick left or right.",
     {"横に　飛びたいときは\nスティックを　左右に　たおそう。",
      "Pour te déplacer horizontalement,\nincline le stick\nvers la gauche ou la droite.",
      "Neige den Stick nach links\noder rechts, um dich horizontal\nüber den Bildschirm zu bewegen.",
      "Se vuoi virare a destra o a sinistra,\ninclina lo stick\nin quella direzione.",
      "Cuando quieras volar hacia los lados,\ninclina el stick\na la izquierda o a la derecha."}},
    {"When you want to fly up or down,\npress [up] or [down].",
     {"上下に　飛びたいときは\n[up]か　[down]を　おそう。", "Pour te déplacer verticalement,\nappuie sur [up] ou [down].",
      "Drücke [up] oder [down], um dich\nvertikal über den Bildschirm\nzu bewegen.",
      "Se vuoi prendere o perdere quota,\npremi [up] o [down].",
      "Cuando quieras volar hacia arriba o\nhacia abajo, pulsa [up] o [down]."}},
    {"When you want to fly up or down,\npush the left stick up or down.",
     {"上下に　飛びたいときは\n左スティックを　上下に　たおそう。",
      "Pour te déplacer verticalement,\nincline le stick gauche\nvers le haut ou le bas.",
      "Neige den linken Stick nach oben\noder unten, um dich vertikal\nüber den Bildschirm zu bewegen.",
      "Se vuoi prendere o perdere quota,\ninclina lo stick sinistro\nin alto o in basso.",
      "Cuando quieras volar hacia arriba o\nhacia abajo, inclina el stick\nizquierdo hacia arriba o abajo."}},
    {"When you want to fly up or down,\npush the stick up or down.",
     {"上下に　飛びたいときは\nスティックを　上下に　たおそう。",
      "Pour te déplacer verticalement,\nincline le stick\nvers le haut ou le bas.",
      "Neige den Stick nach oben\noder unten, um dich vertikal\nüber den Bildschirm zu bewegen.",
      "Se vuoi prendere o perdere quota,\ninclina lo stick\nin alto o in basso.",
      "Cuando quieras volar hacia arriba o\nhacia abajo, inclina el stick\nhacia arriba o abajo."}},
    {"Press [2] while steering\nto move faster.",
     {"移動しながら\n[2]ボタンを　おすと　素早く\n移動できるよ。", "Pour te déplacer rapidement,\nappuie sur [2] en te dirigeant.",
      "Wenn du beim Steuern [2] drückst,\nerhöht sich dein Flugtempo.",
      "Se premi [2] mentre ti dirigi,\nvolerai più velocemente.", "Pulsa [2] mientras te diriges\npara ir más deprisa."}},
    /* The partners' moves */
    {"Oshawott uses Water-type moves.\nHold down [1], then release it\nto use Water Gun. Hold\n[either shake] to use Razor Shell.",
     {"ミジュマルは　水の技が　使える。\n[1]ボタンで　エネルギーを　ためて\nはなすと　みずでっぽう。\n[either shake]を　おしつづけると　シェルブレード。",
      "Moustillon utilise des capacités\nde type Eau.\nAppuie sur [1] pour te concentrer,\net relâche pour utiliser Pistolet à O !\n"
      "Maintiens [either shake] pour\nutiliser Coquilame.",
      "Ottaro setzt Attacken vom Typ\nWasser ein.\nHalte [1] gedrückt, um Energie\naufzuladen, und lass los, um\n"
      "Aquaknarre einzusetzen. Halte\n[either shake], um Kalkklinge\neinzusetzen.",
      "Oshawott usa mosse di tipo Acqua.\nTieni premuto [1] e rilascialo\nper usare Pistolacqua. Tieni premuto\n"
      "[either shake] per usare Conchilama.",
      "Oshawott puede usar movimientos\nde tipo Agua.\nMantén pulsado [1] y suéltalo\npara usar Pistola Agua. Mantén\n"
      "[either shake] para usar Concha Filo."}},
    {"Snivy is good at jumping.\nHold [2] to jump higher.[page]Snivy uses Grass-type moves.\nHold [1] to charge, and release it\n"
     "to use Leaf Storm. Hold\n[either shake] to use Leaf Tornado.",
     {"ツタージャは　ジャンプが　得意だ。\n[2]ボタンを　おしつづけると\n高く　ジャンプできるぞ。[page]ツタージャは　草の技が　使える。\n"
      "[1]ボタンで　エネルギーを　ためて\nはなすと　リーフストーム。\n[either shake]を　おしつづけると　グラスミキサー。",
      "Vipélierre est douée pour le saut.\nMaintiens [2] appuyé pour\nsauter plus haut.[page]Elle maîtrise des capacités de type\n"
      "Plante. Concentre l'énergie en\nmaintenant [1] appuyé et relâche\npour utiliser Tempêteverte.\n"
      "Maintiens [either shake] pour\nutiliser Phytomixeur.",
      "Serpifeu kann sehr hoch springen.\nHältst du [2] gedrückt,\nspringst du höher als sonst![page]Serpifeu setzt im Kampf\n"
      "Pflanzen-Attacken ein.\nLade mit [1] Energie auf,\num Blättersturm zu entfesseln.\nHältst du [either shake],\n"
      "greifst du mit Grasmixer an.",
      "Snivy è bravissima nei salti.\nTieni premuto [2] per saltare molto\nin alto.[page]Le sue mosse sono di tipo Erba.\n"
      "Tieni premuto [1] per accumulare\nenergia. Quando rilasci il pulsante,\nscatenerà Verdebufera.\n"
      "Se tieni premuto [either shake],\nutilizzerà Vorticerba.",
      "Snivy es muy buena saltando.\nSi mantienes pulsado [2], saltará\nmás alto.[page]Usa movimientos de tipo Planta.\n"
      "Mantén pulsado [1] para acumular\nenergía y suéltalo para usar\nLluevehojas. Mantén [either shake]\npara usar Ciclón Hojas."}},
    {"Tepig's Tackle is very powerful!\nTry Dashing into a rock,\nand you might be able to break it.[page]"
     "Tepig uses Fire-type moves.\nHold, then release [1]\nto use Flamethrower. Hold\n[either shake] to use Heat Crash.",
     {"ポカブの　たいあたりは\nとても　強力だ。\nダッシュで　岩に　ぶつかってみよう。[page]ポカブは　炎の技が　使えるぞ。\n"
      "[1]ボタンで　エネルギーを　ためて\nはなすと　かえんほうしゃ。\n[either shake]を　おしつづけると　ヒートスタンプ。",
      "Gruikui possède une attaque\ntrès puissante. Sprinte vers\nun gros rocher pour voir ![page]"
      "Gruikui peut utiliser des capacités\nde type Feu. Garde [1] appuyé\nun moment pour déclencher\n"
      "Lance-Flamme. Maintiens\n[either shake] pour utiliser Tacle Feu.",
      "Floink verfügt über eine sehr starke\nSprintattacke. Versuch einfach mal,\naus dem Sprint heraus Felsen\n"
      "anzurempeln![page]Floink kämpft mit Feuer-Attacken.\nSetze Flammenwurf ein, indem du\n"
      "[1] länger gedrückt hältst und dann\nwieder loslässt. Halte\n[either shake], um Brandstempel\neinzusetzen.",
      "Tepig è in grado di mandare alcuni\ntipi di rocce in frantumi. Prova a\ncolpirne una durante uno sprint.[page]"
      "Le sue mosse sono di tipo Fuoco.\nSe tieni premuto [1] e poi\nrilasci il pulsante, Tepig userà\n"
      "Lanciafiamme. Se tieni premuto\n[either shake], userà Marchiafuoco.",
      "Tepig puede romper rocas usando\nSprint y chocando contra ellas.[page]Para usar Lanzallamas, mantén\n"
      "pulsado [1] y después suéltalo.\nPara usar Golpe Calor,\nmantén [either shake]."}},
    {"You can move this elevator\nby using Snivy's Leaf Tornado.[page]Hold [either shake]\non the elevator\nto use Leaf Tornado.",
     {"このエレベーターは　ツタージャの\nグラスミキサーを　当てることで\n動かすことが　できるぞ。[page]エレベーターの　上で\n"
      "[either shake]を　おしつづけて\nグラスミキサーを　出してみよう。",
      "Cet ascenseur peut être activé grâce\nau Phytomixeur de Vipélierre.[page]Monte sur l'ascenseur et maintiens\n"
      "[either shake] pour utiliser\nPhytomixeur.",
      "Du kannst diesen Aufzug mithilfe\nvon Serpifeus Grasmixer in Betrieb\nnehmen.[page]Setze Grasmixer ein, indem du\n"
      "[either shake] gedrückt hältst, wenn\ndu dich auf dem Aufzug befindest.",
      "Puoi attivare questa\npiattaforma ascensore usando\nla mossa Vorticerba di Snivy![page]Salici sopra e tieni premuto\n"
      "[either shake] per usarla.",
      "Puedes mover este ascensor usando\nel movimiento Ciclón Hojas de Snivy.[page]Colócate sobre el ascensor\n"
      "y mantén [either shake]\npara usar Ciclón Hojas."}},
    {"You can break these blocks by using\nTepig's Tackle or Heat Crash.[page]When you're beside the block,\n"
     "you can smash it with a Tackle.\nWhen you're above the block, hold\n[either shake] to use Heat Crash.",
     {"このブロックは　ポカブの　たいあたりか\nヒートスタンプで　こわせるぞ。[page]ブロックの　横からは\nたいあたりで　こわせる。\n"
      "ブロックの　上では\n[either shake]を　おしつづけて\nヒートスタンプを　出してみよう。",
      "Ce bloc peut être brisé grâce\nau Tacle Feu de Gruikui.[page]Monte sur le bloc et maintiens\n[either shake] pour\n"
      "utiliser Tacle Feu.",
      "Du kannst den Block unter dir\nmit Floinks Brandstempel zerstören\nund so aus dem Weg schaffen![page]"
      "Setze Brandstempel ein, indem du\n[either shake] gedrückt hältst,\nwenn du auf einem Block stehst.",
      "Puoi distruggere il blocco in basso\nusando la mossa Marchiafuoco\ndi Tepig![page]Sali sul blocco e tieni premuto\n"
      "[either shake].",
      "Puedes romper ese bloque usando\nel movimiento Golpe Calor de Tepig.[page]Colócate sobre el bloque\n"
      "y mantén [either shake]\npara usar Golpe Calor."}},
    /* Iron Tail */
    {"In a battle with a different\ncondition for winning, that\ncondition is shown on screen.\nAchieve the goal!\n"
     "Hold [either shake]\nto use Iron Tail.",
     {"いつもとちがう　バトルでは\n勝つための　条件が\n表示される。\n目標を　達成しよう！\n[either shake]を　おしつづけると\n"
      "アイアンテールが　使えるぞ。",
      "Certains duels ont des conditions de\nvictoire spécifiques qui s'affichent.\nRemplis-les pour gagner ![page]"
      "Maintiens [either shake]\npour utiliser Queue de Fer !",
      "Gilt für einen Kampf eine spezielle\nSiegesbedingung, wird diese\nauf dem Bildschirm angezeigt.\n"
      "Erreiche das gesetzte Ziel!\nHalte [either shake],\num Eisenschweif einzusetzen!",
      "Alcune volte dovrai soddisfare\ndeterminate condizioni per vincere\nuna lotta. Metticela tutta![page]"
      "Tieni premuto [either shake]\nper usare Codacciaio.",
      "Cuando las condiciones de victoria\nsean distintas a las de un combate\nnormal, estas aparecerán en\n"
      "pantalla. ¡Intenta cumplir el objetivo!\nMantén [either shake] para usar\nCola Férrea."}},
    /* Shake Sprint */
    {"Hold [either shake] when playing\nChase, and you'll do a special\nkind of Dash called a Shake Sprint.[page]"
     "But be careful--it's hard to turn\nwhen you're going full speed!",
     {"おいかけっこの　勝負中に\n[either shake]を　おしつづけると\n特別速く　ダッシュできる。\nただし　曲がりにくいぞ！　注意しよう。",
      "Pendant la course, maintiens\n[either shake] pour rendre\nle sprint encore plus rapide ![page]"
      "Mais attention, il devient alors\ndifficile de négocier les virages !",
      "Wenn du während des Fangspiels\n[either shake] gedrückt hältst, legst\ndu einen rasend schnellen Sprint hin,\n"
      "aber büßt an Manövrierfähigkeit ein.",
      "Mentre giochi ad acchiapparello,\ntieni premuto [either shake] per fare\nuno sprint speciale. Ma attenzione,\n"
      "perché sarà difficile fare le curve!",
      "Mantén [either shake] mientras\njuegas al pilla pilla para dar un\nimpulso extra a tu Sprint. ¡Pero,\n"
      "cuidado, que será más difícil girar!"}},
    /* The portals */
    {"You'll have to shake more\nto open the portal. Next time,\npress [shake] by turns!",
     {"とびらを　開けるには　ふる\n速さも　大切だ。\n次は　[shake]を\nかわりばんこに　おしてみよう。",
      "L'ouverture de ce portail dépend\naussi de la vitesse à laquelle\ntu secoues. La prochaine fois,\n"
      "appuie tour à tour sur [shake] !",
      "Die Öffnung des Portals hängt auch\ndavon ab, wie schnell du schüttelst.\nDrücke nächstes Mal abwechselnd\n"
      "[shake]!",
      "Per aprire la porta, conta anche\nla velocità. La prossima volta,\npremi a turno [shake]!",
      "También es importante agitar bien\nrápido para abrir el portal.\n¡La próxima vez, pulsa\n"
      "[shake] por turnos!"}},
    {"You'll have to shake a little more\nto open the portal. Next time,\npress [shake] by turns!",
     {"とびらを　開けるには\nもうちょっとだけ　ふる　必要がある。\n次は　[shake]を\nかわりばんこに　おしてみよう。",
      "Il faut secouer encore un peu plus\npour ouvrir ce portail. La prochaine\nfois, appuie tour à tour\nsur [shake] !",
      "Du musst noch ein klein wenig mehr\nschütteln, um das Portal zu öffnen.\nDrücke nächstes Mal abwechselnd\n"
      "[shake]!",
      "Devi scuotere ancora un po' per\naprire la porta. La prossima volta,\npremi a turno [shake]!",
      "Hay que agitar un poquito más\npara abrir el portal. ¡La próxima\nvez, pulsa [shake] por turnos!"}},
    {"Press [shake] by turns to charge!",
     {"[shake]を　かわりばんこに　おして　パワーをためよう！", "Appuie tour à tour sur [shake] pour charger !",
      "Drücke abwechselnd [shake] zum Aufladen!", "Premi a turno [shake] per caricare!",
      "¡Pulsa [shake] por turnos para cargar!"}},
    /* Photos: with the mouse, the right stick, the touch screen */
    {"Press [-] to take Photos.\nThere are two ways to do this.[page]You can simply press [-] to take\n"
     "a Photo of the current screen.[page]You can also hold [B] to look\naround with the mouse, and then\n"
     "press [-] to take a Photo.\nIf you see something you like,\ntry taking a Photo of it!",
     {"[-]ボタンを　おすと\n写真が　とれる。\n写真には\n2種類の　とりかたがあるぞ。[page]ひとつは\n今　映っている　画面を\n"
      "そのまま　[-]ボタンを\nおして　とる　やりかた。[page]もう　ひとつは\n[B]ボタンを　おしたまま\nマウスで　見回して\n"
      "[-]ボタンを　おして\n写真を　とる方法。\n気になる　場所が　あったら\n写真を　とってみよう！",
      "En appuyant sur [-], tu peux\nprendre une photo.\nIl existe deux façons de procéder.[page]La première est simplement\n"
      "d'appuyer sur [-] pour prendre\nl'écran tel qu'il s'affiche.[page]Tu peux aussi maintenir [B] pour\n"
      "regarder autour de toi avec la\nsouris, puis appuyer sur [-] pour\nprendre la photo.\n"
      "Si un lieu te plaît particulièrement,\nprends-le en photo !",
      "Um ein Foto zu schießen, musst du\n[-] drücken. Es gibt zwei\nAufnahmemöglichkeiten.[page]"
      "Du kannst einfach [-] drücken, um\ndas aktuelle Spielgeschehen\nzu fotografieren.[page]"
      "Oder du hältst [B] gedrückt, siehst\ndich mit der Maus um und drückst\ndann [-], um ein Foto zu schießen.\n"
      "Wenn du ein interessantes Motiv\nentdeckst, greif zur Kamera!",
      "Per scattare una foto, premi [-].\nPuoi farne di due tipi.[page]Puoi scattare semplicemente una\n"
      "foto dell'immagine sullo schermo\npremendo [-].[page]Oppure puoi tenere premuto [B] per\n"
      "guardarti in giro con il mouse e\npoi premere [-].\nPotrai così immortalare i luoghi e\n"
      "le scene che più ti incuriosiscono!",
      "Para hacer fotos, pulsa [-].\nHay dos métodos diferentes.[page]El primero consiste en pulsar [-]\n"
      "para hacer una foto de lo que se\nesté viendo en la pantalla.[page]También puedes mantener [B] para\n"
      "mirar a tu alrededor con el ratón\ny pulsar [-] para hacer la foto.\n¡Prueba a hacer fotos de todo\n"
      "lo que te guste!"}},
    {"Press [-] to take Photos.\nThere are two ways to do this.[page]You can simply press [-] to take\n"
     "a Photo of the current screen.[page]You can also hold [B] to look\naround with the right stick, and\n"
     "then press [-] to take a Photo.\nIf you see something you like,\ntry taking a Photo of it!",
     {"[-]ボタンを　おすと\n写真が　とれる。\n写真には\n2種類の　とりかたがあるぞ。[page]ひとつは\n今　映っている　画面を\n"
      "そのまま　[-]ボタンを\nおして　とる　やりかた。[page]もう　ひとつは\n[B]ボタンを　おしたまま\n右スティックで　見回して\n"
      "[-]ボタンを　おして\n写真を　とる方法。\n気になる　場所が　あったら\n写真を　とってみよう！",
      "En appuyant sur [-], tu peux\nprendre une photo.\nIl existe deux façons de procéder.[page]La première est simplement\n"
      "d'appuyer sur [-] pour prendre\nl'écran tel qu'il s'affiche.[page]Tu peux aussi maintenir [B] pour\n"
      "regarder autour de toi avec le\nstick droit, puis appuyer sur [-]\npour prendre la photo.\n"
      "Si un lieu te plaît particulièrement,\nprends-le en photo !",
      "Um ein Foto zu schießen, musst du\n[-] drücken. Es gibt zwei\nAufnahmemöglichkeiten.[page]"
      "Du kannst einfach [-] drücken, um\ndas aktuelle Spielgeschehen\nzu fotografieren.[page]"
      "Oder du hältst [B] gedrückt, siehst\ndich mit dem rechten Stick um und\ndrückst dann [-], um ein Foto\n"
      "zu schießen. Wenn du ein interessantes\nMotiv entdeckst, greif zur Kamera!",
      "Per scattare una foto, premi [-].\nPuoi farne di due tipi.[page]Puoi scattare semplicemente una\n"
      "foto dell'immagine sullo schermo\npremendo [-].[page]Oppure puoi tenere premuto [B] per\n"
      "guardarti in giro con lo stick destro\ne poi premere [-].\nPotrai così immortalare i luoghi e\n"
      "le scene che più ti incuriosiscono!",
      "Para hacer fotos, pulsa [-].\nHay dos métodos diferentes.[page]El primero consiste en pulsar [-]\n"
      "para hacer una foto de lo que se\nesté viendo en la pantalla.[page]También puedes mantener [B] para\n"
      "mirar a tu alrededor con el stick\nderecho y pulsar [-] para hacer\nla foto. ¡Prueba a hacer fotos\n"
      "de todo lo que te guste!"}},
    {"Press [-] to take Photos.\nThere are two ways to do this.[page]You can simply press [-] to take\n"
     "a Photo of the current screen.[page]You can also hold [B] and drag\non the screen to look around, then\n"
     "press [-] to take a Photo.\nIf you see something you like,\ntry taking a Photo of it!",
     {"[-]ボタンを　おすと\n写真が　とれる。\n写真には\n2種類の　とりかたがあるぞ。[page]ひとつは\n今　映っている　画面を\n"
      "そのまま　[-]ボタンを\nおして　とる　やりかた。[page]もう　ひとつは\n[B]ボタンを　おしたまま\n画面を　なぞって　見回して\n"
      "[-]ボタンを　おして\n写真を　とる方法。\n気になる　場所が　あったら\n写真を　とってみよう！",
      "En appuyant sur [-], tu peux\nprendre une photo.\nIl existe deux façons de procéder.[page]La première est simplement\n"
      "d'appuyer sur [-] pour prendre\nl'écran tel qu'il s'affiche.[page]Tu peux aussi maintenir [B] et faire\n"
      "glisser ton doigt sur l'écran pour\nregarder autour de toi, puis appuyer\nsur [-] pour prendre la photo.\n"
      "Si un lieu te plaît particulièrement,\nprends-le en photo !",
      "Um ein Foto zu schießen, musst du\n[-] drücken. Es gibt zwei\nAufnahmemöglichkeiten.[page]"
      "Du kannst einfach [-] drücken, um\ndas aktuelle Spielgeschehen\nzu fotografieren.[page]"
      "Oder du hältst [B] gedrückt und\nwischst über den Bildschirm, um dich\numzusehen, und drückst dann [-].\n"
      "Wenn du ein interessantes Motiv\nentdeckst, greif zur Kamera!",
      "Per scattare una foto, premi [-].\nPuoi farne di due tipi.[page]Puoi scattare semplicemente una\n"
      "foto dell'immagine sullo schermo\npremendo [-].[page]Oppure puoi tenere premuto [B] e\n"
      "trascinare il dito sullo schermo per\nguardarti in giro, poi premere [-].\n"
      "Potrai così immortalare i luoghi e\nle scene che più ti incuriosiscono!",
      "Para hacer fotos, pulsa [-].\nHay dos métodos diferentes.[page]El primero consiste en pulsar [-]\n"
      "para hacer una foto de lo que se\nesté viendo en la pantalla.[page]También puedes mantener [B] y\n"
      "deslizar el dedo por la pantalla\npara mirar y pulsar [-] para\nhacer la foto. ¡Prueba a hacer\n"
      "fotos de todo lo que te guste!"}},
    /* Moving about, the first lesson */
    {"Go up to Piplup, and\npress [2] to talk to him.\nMove with [move].",
     {"ポッチャマに　近づいて\n[2]ボタンで　話しかけてみよう。\n[move]で　移動だ。",
      "Approche-toi de Tiplouf et appuie\nsur [2] pour lui parler.\nPour bouger, utilise [move].",
      "Geh zu Plinfa und sprich ihn mit [2]\nan! Bewege dich mit [move].",
      "Avvicinati a Piplup e premi [2] per\nparlare con lui.\nPer spostarti, usa [move].",
      "Acércate a Piplup y pulsa [2]\npara hablar con él.\nMuévete con [move]."}},
    {"Go up to Piplup, and\npress [2] to talk to him.\nMove with the left stick.",
     {"ポッチャマに　近づいて\n[2]ボタンで　話しかけてみよう。\n左スティックで　移動だ。",
      "Approche-toi de Tiplouf et appuie\nsur [2] pour lui parler.\nPour bouger, utilise le stick gauche.",
      "Geh zu Plinfa und sprich ihn mit [2]\nan! Bewege dich mit dem linken Stick.",
      "Avvicinati a Piplup e premi [2] per\nparlare con lui.\nPer spostarti, usa lo stick sinistro.",
      "Acércate a Piplup y pulsa [2]\npara hablar con él.\nMuévete con el stick izquierdo."}},
    {"Go up to Piplup, and\npress [2] to talk to him.\nMove with the stick.",
     {"ポッチャマに　近づいて\n[2]ボタンで　話しかけてみよう。\nスティックで　移動だ。",
      "Approche-toi de Tiplouf et appuie\nsur [2] pour lui parler.\nPour bouger, utilise le stick.",
      "Geh zu Plinfa und sprich ihn mit [2]\nan! Bewege dich mit dem Stick.",
      "Avvicinati a Piplup e premi [2] per\nparlare con lui.\nPer spostarti, usa lo stick.",
      "Acércate a Piplup y pulsa [2]\npara hablar con él.\nMuévete con el stick."}},
    /* The title screen's Options */
    {"Change the rumble setting\nof the controller.",
     {"コントローラーの　しんどうを\nへんこうします。", "Activer la fonction vibration\nde la manette ?",
      "Schaltet die Rumble-Funktion des\nControllers ein oder aus.", "Regola la funzione rumble\ndel controller.",
      "¿Quieres activar la función de\nvibración del mando?"}},
};
#define ROWS ((int)(sizeof(kRows) / sizeof(kRows[0])))

const char* tr(const char* english) {
    int lang = g_tr_language - 1, i;
    if (!english || lang < 0 || lang >= 5) return english;
    for (i = 0; i < ROWS; i++) {
        if (!strcmp(kRows[i].en, english)) return kRows[i].t[lang] ? kRows[i].t[lang] : english;
    }
    return english;
}

uint32_t tr_next(const char** s) {
    const uint8_t* p = (const uint8_t*)*s;
    uint32_t c = p[0];
    int n = c >= 0xF0 && c < 0xF5 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : 0, i;
    if (!c) return 0;
    if (c >= 0xF5) n = 0;
    for (i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            n = 0; /* not UTF-8: Latin-1 */
            break;
        }
    }
    if (n) c &= 0x3F >> n;
    for (i = 1; i <= n; i++) c = c << 6 | (p[i] & 0x3F);
    *s += 1 + n;
    return c;
}

int tr_put(uint32_t ch, char* out) {
    if (ch < 0x80) {
        out[0] = (char)ch;
        return 1;
    }
    if (ch < 0x800) {
        out[0] = (char)(0xC0 | ch >> 6);
        out[1] = (char)(0x80 | (ch & 0x3F));
        return 2;
    }
    if (ch < 0x10000) {
        out[0] = (char)(0xE0 | ch >> 12);
        out[1] = (char)(0x80 | (ch >> 6 & 0x3F));
        out[2] = (char)(0x80 | (ch & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | ch >> 18);
    out[1] = (char)(0x80 | (ch >> 12 & 0x3F));
    out[2] = (char)(0x80 | (ch >> 6 & 0x3F));
    out[3] = (char)(0x80 | (ch & 0x3F));
    return 4;
}
