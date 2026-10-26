#pragma once
namespace cv_launcher_maps {
enum Id {Title,Intro,Enable,Choose,Shortcuts,On,Off,StartHint,RunningHint,Count};
inline constexpr const wchar_t* texts[13][Count]={
{L"Mapas e saves",L"Durante a partida, escolha um mapa, salve em qualquer lugar ou carregue um save.",L"Ativar seleção de mapas",L"Escolher mapa",L"Atalhos — teclado e controle",L"Ativado",L"Desativado",L"Inicie uma partida e use um dos atalhos abaixo para abrir o menu.",L"Use os atalhos abaixo para escolher mapas, salvar ou carregar."},
{L"Maps and saves",L"During gameplay, choose a map, save anywhere or load a save.",L"Enable map selection",L"Choose map",L"Shortcuts — keyboard and controller",L"Enabled",L"Disabled",L"Start a game and use one of the shortcuts below to open the menu.",L"Use the shortcuts below to choose maps, save or load."},
{L"Mapas y partidas guardadas",L"Durante la partida, elige un mapa, guarda en cualquier lugar o carga una partida.",L"Activar selección de mapas",L"Elegir mapa",L"Atajos — teclado y mando",L"Activado",L"Desactivado",L"Inicia una partida y usa uno de los atajos de abajo para abrir el menú.",L"Usa los atajos de abajo para elegir mapas, guardar o cargar."},
{L"マップとセーブ",L"プレイ中にマップを選択し、どこでもセーブしたり、セーブデータをロードしたりできます。",L"マップ選択を有効にする",L"マップを選ぶ",L"ショートカット — キーボードとコントローラー",L"有効",L"無効",L"ゲームを開始し、下のショートカットでメニューを開いてください。",L"下のショートカットでマップ選択・セーブ・ロードを行えます。"},
{L"Cartes et sauvegardes",L"En partie, choisissez une carte, sauvegardez partout ou chargez une sauvegarde.",L"Activer la sélection de carte",L"Choisir une carte",L"Raccourcis — clavier et manette",L"Activé",L"Désactivé",L"Lancez une partie et utilisez un raccourci ci-dessous pour ouvrir le menu.",L"Utilisez les raccourcis pour choisir une carte, sauvegarder ou charger."},
{L"Karten und Spielstände",L"Wähle im Spiel eine Karte, speichere überall oder lade einen Spielstand.",L"Kartenauswahl aktivieren",L"Karte wählen",L"Tastenkürzel — Tastatur und Controller",L"Aktiviert",L"Deaktiviert",L"Starte ein Spiel und öffne das Menü mit einem der Kürzel unten.",L"Nutze die Kürzel unten für Kartenauswahl, Speichern oder Laden."},
{L"Mappe e salvataggi",L"Durante la partita, scegli una mappa, salva ovunque o carica un salvataggio.",L"Attiva selezione mappe",L"Scegli mappa",L"Scorciatoie — tastiera e controller",L"Attivata",L"Disattivata",L"Avvia una partita e usa una scorciatoia qui sotto per aprire il menu.",L"Usa le scorciatoie per scegliere mappe, salvare o caricare."},
{L"Mapy i zapisy gry",L"Podczas gry wybierz mapę, zapisz w dowolnym miejscu lub wczytaj zapis.",L"Włącz wybór mapy",L"Wybierz mapę",L"Skróty — klawiatura i kontroler",L"Włączony",L"Wyłączony",L"Rozpocznij grę i otwórz menu jednym z poniższych skrótów.",L"Użyj skrótów poniżej, aby wybrać mapę, zapisać lub wczytać grę."},
{L"Haritalar ve kayıtlar",L"Oyun sırasında harita seçin, istediğiniz yerde kaydedin veya kayıt yükleyin.",L"Harita seçimini etkinleştir",L"Harita seç",L"Kısayollar — klavye ve kontrolcü",L"Etkin",L"Devre dışı",L"Oyunu başlatıp aşağıdaki kısayollardan biriyle menüyü açın.",L"Harita seçmek, kaydetmek veya yüklemek için aşağıdaki kısayolları kullanın."},
{L"الخرائط وملفات الحفظ",L"أثناء اللعب، اختر خريطة أو احفظ في أي مكان أو حمّل ملف حفظ.",L"تفعيل اختيار الخريطة",L"اختيار خريطة",L"الاختصارات — لوحة المفاتيح ووحدة التحكم",L"مفعّل",L"معطّل",L"ابدأ اللعب واستخدم أحد الاختصارات أدناه لفتح القائمة.",L"استخدم الاختصارات أدناه لاختيار الخرائط أو الحفظ أو التحميل."},
{L"地图与存档",L"游戏过程中可以选择地图、随时保存或读取存档。",L"启用地图选择",L"选择地图",L"快捷键 — 键盘和控制器",L"已启用",L"已禁用",L"开始游戏后，使用下方的快捷键打开菜单。",L"使用下方快捷键选择地图、保存或读取存档。"},
{L"맵 및 저장",L"플레이 중 맵을 선택하고, 어디서나 저장하거나 저장 파일을 불러올 수 있습니다.",L"맵 선택 활성화",L"맵 선택하기",L"단축키 — 키보드와 컨트롤러",L"활성화",L"비활성화",L"게임을 시작한 뒤 아래 단축키로 메뉴를 여세요.",L"아래 단축키로 맵 선택, 저장 또는 불러오기를 실행하세요."},
{L"Карты и сохранения",L"Во время игры выбирайте карту, сохраняйтесь где угодно или загружайте сохранение.",L"Включить выбор карты",L"Выбрать карту",L"Сочетания — клавиатура и контроллер",L"Включено",L"Выключено",L"Запустите игру и откройте меню одним из сочетаний ниже.",L"Используйте сочетания ниже для выбора карты, сохранения или загрузки."},
};
constexpr bool complete(){for(const auto& row:texts)for(auto text:row)if(!text||!*text)return false;return true;}
static_assert(complete());
inline const wchar_t* get(int lang,Id id){return texts[lang>=0&&lang<13?lang:1][id];}
}
