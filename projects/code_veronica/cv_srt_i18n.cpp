#include "cv_srt_i18n.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include "launcher_i18n.h"
namespace cv_srt_i18n {
namespace {
std::wstring original_item(int id,bool pt) {
    struct Named {int id;const wchar_t* en;const wchar_t* pt;};
    static const Named names[]={
        {1,L"Rocket Launcher",L"Lança-foguetes"},{2,L"Assault Rifle",L"Fuzil de assalto"},{3,L"Sniper Rifle",L"Rifle de precisão"},
        {4,L"Shotgun",L"Espingarda"},{5,L"Glock 17",L"Glock 17"},{6,L"Grenade Launcher",L"Lança-granadas"},{7,L"Bow Gun",L"Besta"},
        {8,L"Combat Knife",L"Faca de combate"},{9,L"Handgun",L"Pistola"},{10,L"Custom Handgun",L"Pistola personalizada"},
        {11,L"Linear Launcher",L"Canhão linear"},{12,L"Handgun Bullets",L"Munição de pistola"},{13,L"Magnum Bullets",L"Munição de Magnum"},
        {14,L"Shotgun Shells",L"Cartuchos"},{15,L"Grenade Rounds",L"Granadas"},{16,L"Acid Rounds",L"Granadas ácidas"},
        {17,L"Flame Rounds",L"Granadas incendiárias"},{18,L"Bow Gun Arrows",L"Flechas"},{19,L"M93R Parts",L"Peças da M93R"},
        {20,L"First Aid Spray",L"Spray de primeiros socorros"},{21,L"Green Herb",L"Erva verde"},{22,L"Red Herb",L"Erva vermelha"},
        {23,L"Blue Herb",L"Erva azul"},{24,L"Mixed Herbs (G+G)",L"Ervas (V+V)"},{25,L"Mixed Herbs (G+R)",L"Ervas (V+Vm)"},
        {26,L"Mixed Herbs (G+B)",L"Ervas (V+A)"},{27,L"Mixed Herbs (G+G+B)",L"Ervas (V+V+A)"},
        {28,L"Mixed Herbs (G+G+G)",L"Ervas (V+V+V)"},{29,L"Mixed Herbs (G+R+B)",L"Ervas (V+Vm+A)"},
        {30,L"Magnum Bullets",L"Munição de Magnum"},{31,L"Ink Ribbon",L"Fita de tinta"},{32,L"Magnum",L"Magnum"},
        {33,L"Gold Lugers",L"Lugers douradas"},{34,L"Submachine Guns",L"Submetralhadoras"},{35,L"Bow Gun Powder",L"Pólvora para flechas"},
        {36,L"Explosive Arrows",L"Flechas explosivas"},{37,L"B.O.W. Gas Rounds",L"Granadas B.O.W."},
        {38,L"Machine Gun Bullets",L"Munição de metralhadora"},{39,L"Gas Mask",L"Máscara de gás"},
        {40,L"Rifle Bullets",L"Munição de rifle"},{42,L"Assault Rifle Bullets",L"Munição de fuzil"},
        {0x29,L"Duralumin Case",L"Maleta de duralumínio"},
        {0x2B,L"Alexander's Pierce",L"Brinco de Alexander"},
        {0x2C,L"Alexander's Jewel",L"Joia de Alexander"},
        {0x2D,L"Alfred's Ring",L"Anel de Alfred"},
        {0x2E,L"Alfred's Jewel",L"Joia de Alfred"},
        {0x2F,L"Prisoner's Diary",L"Diário do prisioneiro"},
        {0x30,L"Director's Memo",L"Memorando do diretor"},
        {0x31,L"Instructions",L"Instruções"},
        {0x32,L"Lockpick",L"Gazua"},
        {0x33,L"Glass Eye",L"Olho de vidro"},
        {0x34,L"Piano Roll",L"Rolo de pianola"},
        {0x35,L"Steering Wheel",L"Timão"},
        {0x36,L"Crane Key",L"Chave do guindaste"},
        {0x37,L"Lighter",L"Isqueiro"},
        {0x38,L"Eagle Plate",L"Placa da águia"},
        {0x39,L"Side Pack",L"Bolsa lateral"},
        {0x3A,L"Map Roll",L"Rolo de mapas"},
        {0x3B,L"Hawk Emblem",L"Emblema do falcão"},
        {0x3C,L"Queen Ant Object",L"Objeto da formiga rainha"},
        {0x3D,L"King Ant Object",L"Objeto da formiga rei"},
        {0x3E,L"Biohazard Card",L"Cartão Biohazard"},
        {0x3F,L"Case: M93R Parts",L"Maleta: peças da M93R"},
        {0x40,L"Detonator",L"Detonador"},
        {0x41,L"Control Lever",L"Alavanca de controle"},
        {0x42,L"Gold Dragonfly",L"Libélula dourada"},
        {0x43,L"Silver Key",L"Chave de prata"},
        {0x44,L"Gold Key",L"Chave de ouro"},
        {0x45,L"Army Proof",L"Símbolo do Exército"},
        {0x46,L"Navy Proof",L"Símbolo da Marinha"},
        {0x47,L"Air Force Proof",L"Símbolo da Aeronáutica"},
        {0x48,L"Key with Tag",L"Chave com etiqueta"},
        {0x49,L"ID Card",L"Cartão de identificação"},
        {0x4A,L"Map",L"Mapa"},
        {0x4B,L"Airport Key",L"Chave do aeroporto"},
        {0x4C,L"Emblem Card",L"Cartão com emblema"},
        {0x4D,L"Skeleton Picture",L"Quadro do esqueleto"},
        {0x4E,L"Music Box Plate",L"Placa da caixa de música"},
        {0x4F,L"Gold Dragonfly (Body)",L"Libélula dourada (corpo)"},
        {0x50,L"Album",L"Álbum"},
        {0x51,L"Halberd",L"Alabarda"},
        {0x52,L"Fire Extinguisher",L"Extintor"},
        {0x53,L"Briefcase",L"Maleta"},
        {0x54,L"Padlock Key",L"Chave do cadeado"},
        {0x55,L"TG-01",L"TG-01"},
        {0x56,L"Special Alloy Emblem",L"Emblema de liga especial"},
        {0x57,L"Valve Handle",L"Manivela da válvula"},
        {0x58,L"Octagonal Valve Handle",L"Manivela octogonal"},
        {0x59,L"Machine Room Key",L"Chave da sala de máquinas"},
        {0x5A,L"Mining Room Key",L"Chave da sala de mineração"},
        {0x5B,L"Barcode Sticker",L"Etiqueta de código de barras"},
        {0x5C,L"Sterile Room Key",L"Chave da sala esterilizada"},
        {0x5D,L"Door Knob",L"Maçaneta"},
        {0x5E,L"Battery Pack",L"Bateria"},
        {0x5F,L"Hemostatic Wire",L"Fio hemostático"},
        {0x60,L"Turntable Key",L"Chave da plataforma"},
        {0x61,L"Chemical Storage Key",L"Chave do depósito químico"},
        {0x62,L"Clement Alpha",L"Clement Alpha"},
        {0x63,L"Clement Sigma",L"Clement Sigma"},
        {0x64,L"Tank Object",L"Objeto do tanque"},
        {0x65,L"Special Alloy Emblem",L"Emblema de liga especial"},
        {0x66,L"Alfred's Memo",L"Memorando de Alfred"},
        {0x67,L"Rusted Sword",L"Espada enferrujada"},
        {0x68,L"Hemostatic Medicine",L"Hemostático"},
        {0x69,L"Security Card",L"Cartão de segurança"},
        {0x6A,L"Security File",L"Arquivo de segurança"},
        {0x6B,L"Alexia's Choker",L"Gargantilha de Alexia"},
        {0x6C,L"Alexia's Jewel",L"Joia de Alexia"},
        {0x6D,L"Queen Ant Relief",L"Relevo da formiga rainha"},
        {0x6E,L"King Ant Relief",L"Relevo da formiga rei"},
        {0x6F,L"Red Jewel",L"Joia vermelha"},
        {0x70,L"Blue Jewel",L"Joia azul"},
        {0x71,L"Socket",L"Soquete"},
        {0x72,L"Square Valve Handle",L"Manivela quadrada"},
        {0x73,L"Serum",L"Soro"},
        {0x74,L"Earthenware Vase",L"Vaso de cerâmica"},
        {0x75,L"Paperweight",L"Peso de papel"},
        {0x76,L"Silver Dragonfly (Body)",L"Libélula prateada (corpo)"},
        {0x77,L"Silver Dragonfly",L"Libélula prateada"},
        {0x78,L"Wing Object",L"Objeto de asa"},
        {0x79,L"Crystal",L"Cristal"},
        {0x7A,L"Gold Dragonfly (1 Wing)",L"Libélula dourada (1 asa)"},
        {0x7B,L"Gold Dragonfly (2 Wings)",L"Libélula dourada (2 asas)"},
        {0x7C,L"Gold Dragonfly (3 Wings)",L"Libélula dourada (3 asas)"},
        {0x7D,L"File",L"Arquivo"},
        {0x7E,L"Plant Pot",L"Vaso de planta"},
        {0x7F,L"Picture B",L"Fotografia B"},
        {0x80,L"Case: Bow Gun Powder",L"Maleta: pólvora para flechas"},
        {0x81,L"Case: Magnum Rounds",L"Maleta: munição de Magnum"},
        {0x82,L"Bow Gun Powder",L"Pólvora para flechas"},
        {0x83,L"Enhanced Handgun",L"Pistola aprimorada"},
        {0x84,L"Memo",L"Memorando"},
        {0x85,L"Board Clip",L"Recorte"},
        {0x86,L"Card",L"Cartão"},
        {0x87,L"Newspaper Clipping",L"Recorte de jornal"},
        {0x88,L"Luger Replica",L"Réplica de Luger"},
        {0x89,L"Queen Ant Relief (Complete)",L"Relevo da rainha (completo)"},
        {0x8A,L"Family Picture",L"Foto de família"},
        {0x8B,L"File Folders",L"Pastas de arquivos"},
        {0x8C,L"Remote Controller",L"Controle remoto"},
        {0x8D,L"Unknown Item (8D)",L"Item desconhecido (8D)"},
        {0x8E,L"M1P",L"M1P"},
        {0x8F,L"Calico Bullets",L"Munição de Calico"},
        {0x90,L"Clement Mixture",L"Mistura Clement"},
        {0x91,L"Playing Manual",L"Manual de jogo"},
        {0x92,L"Unknown Item (92)",L"Item desconhecido (92)"},
        {0x93,L"Unknown Item (93)",L"Item desconhecido (93)"},
        {0x94,L"Unknown Item (94)",L"Item desconhecido (94)"},
        {0x95,L"Empty Extinguisher",L"Extintor vazio"},
        {0x96,L"Square Socket",L"Soquete quadrado"},
        {0x97,L"Unknown Item (97)",L"Item desconhecido (97)"},
        {0x98,L"Crest Key S",L"Chave de brasão S"},
        {0x99,L"Crest Key G",L"Chave de brasão G"}
    };
    if(!id)return pt?L"Vazio":L"Empty";
    for(const auto& n:names)if(n.id==id)return pt?n.pt:n.en;
    wchar_t b[32];swprintf(b,32,pt?L"Item %02X":L"Item %02X",id);return b;
}
enum Term { Rocket,Assault,Sniper,Shotgun,GrenadeLauncher,BowGun,Knife,Handgun,CustomHandgun,LinearLauncher,HandgunAmmo,MagnumAmmo,Shells,Grenades,Acid,Flame,Arrows,Parts,FirstAid,GreenHerb,RedHerb,BlueHerb,MixedHerbs,Green,Red,Blue,InkRibbon,GoldLugers,SMGs,BowPowder,ExplosiveArrows,BOWGas,MGAmmo,GasMask,RifleAmmo,AssaultAmmo,Case,DuraluminCase,Pierce,Jewel,Ring,PrisonerDiary,DirectorMemo,Instructions,Lockpick,GlassEye,PianoRoll,Wheel,CraneKey,Lighter,EaglePlate,SidePack,MapRoll,HawkEmblem,QueenObject,KingObject,BiohazardCard,Detonator,ControlLever,GoldDragonfly,SilverKey,GoldKey,ArmyProof,NavyProof,AirProof,TaggedKey,IDCard,Map,AirportKey,EmblemCard,SkeletonPicture,MusicPlate,Body,Album,Halberd,Extinguisher,PadlockKey,AlloyEmblem,Valve,OctValve,MachineKey,MiningKey,Barcode,SterileKey,DoorKnob,Battery,HemostaticWire,TurntableKey,ChemicalKey,TankObject,Memo,RustedSword,Hemostatic,SecurityCard,SecurityFile,Choker,QueenRelief,KingRelief,RedJewel,BlueJewel,Socket,SquareValve,Serum,Vase,Paperweight,SilverDragonfly,Wing,Crystal,File,PlantPot,Picture,EnhancedHandgun,BoardClip,Card,Newspaper,LugerReplica,Complete,FamilyPicture,Folders,Remote,Mixture,Manual,Empty,Unknown,SquareSocket,CrestKey,Zombie,Spider,ZombieDog,Moth,Bat,MutatedSteve,GiantSpider,Anatomist,Tentacle,Enemy,Ammo };
const wchar_t* const terms[][11]={
    {L"Lanzacohetes",L"ロケットランチャー",L"Lance-roquettes",L"Raketenwerfer",L"Lanciarazzi",L"Wyrzutnia rakiet",L"Roketatar",L"قاذف صواريخ",L"火箭筒",L"로켓 발사기",L"Ракетница"}, // Rocket
    {L"Fusil de asalto",L"アサルトライフル",L"Fusil d’assaut",L"Sturmgewehr",L"Fucile d’assalto",L"Karabin szturmowy",L"Taarruz tüfeği",L"بندقية هجومية",L"突击步枪",L"돌격소총",L"Штурмовая винтовка"}, // Assault
    {L"Rifle de francotirador",L"スナイパーライフル",L"Fusil de précision",L"Scharfschützengewehr",L"Fucile di precisione",L"Karabin snajperski",L"Keskin nişancı tüfeği",L"بندقية قنص",L"狙击步枪",L"저격소총",L"Снайперская винтовка"}, // Sniper
    {L"Escopeta",L"ショットガン",L"Fusil à pompe",L"Schrotflinte",L"Fucile a pompa",L"Strzelba",L"Pompalı tüfek",L"بندقية خرطوش",L"霰弹枪",L"산탄총",L"Дробовик"}, // Shotgun
    {L"Lanzagranadas",L"グレネードランチャー",L"Lance-grenades",L"Granatwerfer",L"Lanciagranate",L"Granatnik",L"Bombaatar",L"قاذف قنابل",L"榴弹发射器",L"유탄 발사기",L"Гранатомёт"}, // GrenadeLauncher
    {L"Ballesta",L"ボウガン",L"Arbalète",L"Armbrust",L"Balestra",L"Kusza",L"Arbalet",L"قوس نشاب",L"弩",L"보우건",L"Арбалет"}, // BowGun
    {L"Cuchillo de combate",L"コンバットナイフ",L"Couteau de combat",L"Kampfmesser",L"Coltello da combattimento",L"Nóż bojowy",L"Savaş bıçağı",L"سكين قتالية",L"战斗刀",L"전투용 단검",L"Боевой нож"}, // Knife
    {L"Pistola",L"ハンドガン",L"Pistolet",L"Pistole",L"Pistola",L"Pistolet",L"Tabanca",L"مسدس",L"手枪",L"권총",L"Пистолет"}, // Handgun
    {L"Pistola modificada",L"カスタムハンドガン",L"Pistolet modifié",L"Modifizierte Pistole",L"Pistola modificata",L"Zmodyfikowany pistolet",L"Modifiye tabanca",L"مسدس معدل",L"改装手枪",L"개조 권총",L"Модифицированный пистолет"}, // CustomHandgun
    {L"Cañón lineal",L"リニアランチャー",L"Canon linéaire",L"Linearkanone",L"Cannone lineare",L"Działo liniowe",L"Doğrusal top",L"مدفع خطي",L"线性发射器",L"리니어 런처",L"Линейная пушка"}, // LinearLauncher
    {L"Munición de pistola",L"ハンドガンの弾",L"Munitions de pistolet",L"Pistolenmunition",L"Munizioni per pistola",L"Amunicja do pistoletu",L"Tabanca mermileri",L"ذخيرة مسدس",L"手枪弹药",L"권총 탄약",L"Патроны для пистолета"}, // HandgunAmmo
    {L"Munición de Magnum",L"マグナムの弾",L"Munitions de Magnum",L"Magnum-Munition",L"Munizioni per Magnum",L"Amunicja Magnum",L"Magnum mermileri",L"ذخيرة ماغنوم",L"马格南弹药",L"매그넘 탄약",L"Патроны для магнума"}, // MagnumAmmo
    {L"Cartuchos de escopeta",L"ショットガンの弾",L"Cartouches de fusil",L"Schrotpatronen",L"Cartucce per fucile",L"Naboje do strzelby",L"Pompalı tüfek fişekleri",L"خراطيش بندقية",L"霰弹枪弹药",L"산탄총 탄약",L"Патроны для дробовика"}, // Shells
    {L"Granadas",L"グレネード弾",L"Grenades",L"Granaten",L"Granate",L"Granaty",L"Bomba mermileri",L"قنابل",L"榴弹",L"유탄",L"Гранаты"}, // Grenades
    {L"Granadas ácidas",L"硫酸弾",L"Grenades acides",L"Säuregranaten",L"Granate acide",L"Granaty kwasowe",L"Asit mermileri",L"قنابل حمضية",L"硫酸弹",L"황산탄",L"Кислотные гранаты"}, // Acid
    {L"Granadas incendiarias",L"火炎弾",L"Grenades incendiaires",L"Brandgranaten",L"Granate incendiarie",L"Granaty zapalające",L"Alev mermileri",L"قنابل حارقة",L"火焰弹",L"화염탄",L"Зажигательные гранаты"}, // Flame
    {L"Flechas de ballesta",L"ボウガンの矢",L"Flèches d’arbalète",L"Armbrustbolzen",L"Dardi per balestra",L"Bełty do kuszy",L"Arbalet okları",L"سهام قوس نشاب",L"弩箭",L"보우건 화살",L"Арбалетные стрелы"}, // Arrows
    {L"Piezas",L"パーツ",L"Pièces",L"Teile",L"Componenti",L"Części",L"Parçalar",L"قطع",L"零件",L"부품",L"Детали"}, // Parts
    {L"Spray de primeros auxilios",L"救急スプレー",L"Spray de premiers secours",L"Erste-Hilfe-Spray",L"Spray medico",L"Spray leczniczy",L"İlk yardım spreyi",L"بخاخ إسعافات أولية",L"急救喷雾",L"구급 스프레이",L"Лечебный спрей"}, // FirstAid
    {L"Hierba verde",L"グリーンハーブ",L"Herbe verte",L"Grünes Kraut",L"Erba verde",L"Zielone zioło",L"Yeşil ot",L"عشبة خضراء",L"绿草药",L"녹색 허브",L"Зелёная трава"}, // GreenHerb
    {L"Hierba roja",L"レッドハーブ",L"Herbe rouge",L"Rotes Kraut",L"Erba rossa",L"Czerwone zioło",L"Kırmızı ot",L"عشبة حمراء",L"红草药",L"붉은 허브",L"Красная трава"}, // RedHerb
    {L"Hierba azul",L"ブルーハーブ",L"Herbe bleue",L"Blaues Kraut",L"Erba blu",L"Niebieskie zioło",L"Mavi ot",L"عشبة زرقاء",L"蓝草药",L"파란 허브",L"Синяя трава"}, // BlueHerb
    {L"Mezcla de hierbas",L"調合ハーブ",L"Mélange d’herbes",L"Kräutermischung",L"Erbe miste",L"Mieszanka ziół",L"Ot karışımı",L"أعشاب مخلوطة",L"混合草药",L"혼합 허브",L"Смесь трав"}, // MixedHerbs
    {L"Verde",L"緑",L"Vert",L"Grün",L"Verde",L"Zielony",L"Yeşil",L"أخضر",L"绿",L"녹",L"З"}, // Green
    {L"Rojo",L"赤",L"Rouge",L"Rot",L"Rosso",L"Czerwony",L"Kırmızı",L"أحمر",L"红",L"적",L"К"}, // Red
    {L"Azul",L"青",L"Bleu",L"Blau",L"Blu",L"Niebieski",L"Mavi",L"أزرق",L"蓝",L"청",L"С"}, // Blue
    {L"Cinta de tinta",L"インクリボン",L"Ruban encreur",L"Farbband",L"Nastro inchiostratore",L"Taśma barwiąca",L"Mürekkep şeridi",L"شريط حبر",L"色带",L"잉크 리본",L"Чернильная лента"}, // InkRibbon
    {L"Lugers doradas",L"ゴールドルガー",L"Lugers dorés",L"Goldene Lugers",L"Luger dorate",L"Złote Lugery",L"Altın Luger'lar",L"مسدسا لوغر ذهبيان",L"黄金鲁格手枪",L"황금 루거",L"Золотые люгеры"}, // GoldLugers
    {L"Subfusiles",L"サブマシンガン",L"Pistolets-mitrailleurs",L"Maschinenpistolen",L"Mitragliette",L"Pistolety maszynowe",L"Hafif makineli tüfekler",L"رشاشان خفيفان",L"冲锋枪",L"기관단총",L"Пистолеты-пулемёты"}, // SMGs
    {L"Pólvora para flechas",L"ボウガン用火薬",L"Poudre pour flèches",L"Pulver für Armbrustbolzen",L"Polvere per dardi",L"Proch do bełtów",L"Ok barutu",L"بارود سهام",L"弩箭火药",L"화살용 화약",L"Порох для стрел"}, // BowPowder
    {L"Flechas explosivas",L"火薬付きボウガンの矢",L"Flèches explosives",L"Explosivbolzen",L"Dardi esplosivi",L"Wybuchowe bełty",L"Patlayıcı oklar",L"سهام متفجرة",L"爆炸弩箭",L"폭발 화살",L"Взрывные стрелы"}, // ExplosiveArrows
    {L"Granadas de gas B.O.W.",L"B.O.W.ガス弾",L"Grenades de gaz B.O.W.",L"B.O.W.-Gasgranaten",L"Granate a gas B.O.W.",L"Granaty gazowe B.O.W.",L"B.O.W. gaz mermileri",L"قنابل غاز B.O.W.",L"B.O.W.瓦斯弹",L"B.O.W. 가스탄",L"Газовые гранаты B.O.W."}, // BOWGas
    {L"Munición de subfusil",L"サブマシンガンの弾",L"Munitions de mitraillette",L"Maschinenpistolenmunition",L"Munizioni per mitraglietta",L"Amunicja do pistoletu maszynowego",L"Hafif makineli tüfek mermileri",L"ذخيرة رشاش",L"冲锋枪弹药",L"기관단총 탄약",L"Патроны для пистолета-пулемёта"}, // MGAmmo
    {L"Máscara de gas",L"ガスマスク",L"Masque à gaz",L"Gasmaske",L"Maschera antigas",L"Maska gazowa",L"Gaz maskesi",L"قناع غاز",L"防毒面具",L"방독면",L"Противогаз"}, // GasMask
    {L"Munición de rifle",L"ライフルの弾",L"Munitions de fusil",L"Gewehrmunition",L"Munizioni per fucile",L"Amunicja do karabinu",L"Tüfek mermileri",L"ذخيرة بندقية",L"步枪弹药",L"소총 탄약",L"Патроны для винтовки"}, // RifleAmmo
    {L"Munición de fusil de asalto",L"アサルトライフルの弾",L"Munitions de fusil d’assaut",L"Sturmgewehrmunition",L"Munizioni per fucile d’assalto",L"Amunicja do karabinu szturmowego",L"Taarruz tüfeği mermileri",L"ذخيرة بندقية هجومية",L"突击步枪弹药",L"돌격소총 탄약",L"Патроны для штурмовой винтовки"}, // AssaultAmmo
    {L"Maletín",L"ケース",L"Mallette",L"Koffer",L"Valigetta",L"Walizka",L"Çanta",L"حقيبة",L"手提箱",L"서류 가방",L"Кейс"}, // Case
    {L"Maletín de duraluminio",L"ジュラルミンケース",L"Mallette en duralumin",L"Duraluminiumkoffer",L"Valigetta in duralluminio",L"Walizka z duraluminium",L"Duralümin çanta",L"حقيبة دورالومين",L"硬铝手提箱",L"두랄루민 케이스",L"Дюралевый кейс"}, // DuraluminCase
    {L"Pendiente",L"ピアス",L"Boucle d’oreille",L"Ohrring",L"Orecchino",L"Kolczyk",L"Küpe",L"قرط",L"耳环",L"귀걸이",L"Серьга"}, // Pierce
    {L"Joya",L"宝石",L"Joyau",L"Juwel",L"Gioiello",L"Klejnot",L"Mücevher",L"جوهرة",L"宝石",L"보석",L"Драгоценность"}, // Jewel
    {L"Anillo",L"指輪",L"Bague",L"Ring",L"Anello",L"Pierścień",L"Yüzük",L"خاتم",L"戒指",L"반지",L"Кольцо"}, // Ring
    {L"Diario del prisionero",L"囚人の日記",L"Journal du prisonnier",L"Tagebuch des Gefangenen",L"Diario del prigioniero",L"Dziennik więźnia",L"Mahkûmun günlüğü",L"مذكرات السجين",L"囚犯日记",L"죄수의 일기",L"Дневник заключённого"}, // PrisonerDiary
    {L"Nota del director",L"所長のメモ",L"Note du directeur",L"Notiz des Direktors",L"Nota del direttore",L"Notatka dyrektora",L"Müdürün notu",L"مذكرة المدير",L"所长备忘录",L"소장의 메모",L"Записка директора"}, // DirectorMemo
    {L"Instrucciones",L"指示書",L"Instructions",L"Anweisungen",L"Istruzioni",L"Instrukcje",L"Talimatlar",L"تعليمات",L"说明书",L"지침서",L"Инструкции"}, // Instructions
    {L"Ganzúa",L"キーピック",L"Crochet",L"Dietrich",L"Grimaldello",L"Wytrych",L"Maymuncuk",L"أداة فتح أقفال",L"开锁器",L"락픽",L"Отмычка"}, // Lockpick
    {L"Ojo de cristal",L"ガラスの目玉",L"Œil de verre",L"Glasauge",L"Occhio di vetro",L"Szklane oko",L"Cam göz",L"عين زجاجية",L"玻璃眼珠",L"유리 눈알",L"Стеклянный глаз"}, // GlassEye
    {L"Rollo de pianola",L"ピアノロール",L"Rouleau de piano",L"Notenrolle",L"Rullo per pianola",L"Rolka pianolowa",L"Piyano rulosu",L"لفافة بيانو",L"自动钢琴纸卷",L"피아노 롤",L"Валик для пианолы"}, // PianoRoll
    {L"Timón",L"操舵輪",L"Gouvernail",L"Steuerrad",L"Timone",L"Koło sterowe",L"Dümen",L"عجلة قيادة",L"舵轮",L"조타륜",L"Штурвал"}, // Wheel
    {L"Llave de la grúa",L"クレーンの鍵",L"Clé de la grue",L"Kranschlüssel",L"Chiave della gru",L"Klucz do dźwigu",L"Vinç anahtarı",L"مفتاح الرافعة",L"起重机钥匙",L"크레인 열쇠",L"Ключ от крана"}, // CraneKey
    {L"Mechero",L"ライター",L"Briquet",L"Feuerzeug",L"Accendino",L"Zapalniczka",L"Çakmak",L"قداحة",L"打火机",L"라이터",L"Зажигалка"}, // Lighter
    {L"Placa del águila",L"鷲のプレート",L"Plaque de l’aigle",L"Adlerplatte",L"Piastra dell’aquila",L"Płyta z orłem",L"Kartal plakası",L"لوحة النسر",L"鹰之石板",L"독수리 문양 판",L"Пластина с орлом"}, // EaglePlate
    {L"Bolsa lateral",L"サイドパック",L"Sacoche",L"Seitentasche",L"Borsa laterale",L"Torba boczna",L"Yan çanta",L"حقيبة جانبية",L"腰包",L"사이드 팩",L"Поясная сумка"}, // SidePack
    {L"Rollo de mapas",L"地図の巻物",L"Rouleau de cartes",L"Kartenrolle",L"Rotolo di mappe",L"Zwój map",L"Harita rulosu",L"لفافة خرائط",L"地图卷轴",L"지도 두루마리",L"Свиток карт"}, // MapRoll
    {L"Emblema del halcón",L"鷹のエンブレム",L"Emblème du faucon",L"Falkenemblem",L"Emblema del falco",L"Emblemat sokoła",L"Şahin amblemi",L"شعار الصقر",L"隼之徽章",L"매의 문장",L"Эмблема сокола"}, // HawkEmblem
    {L"Objeto de hormiga reina",L"女王蟻のオブジェ",L"Objet de la reine fourmi",L"Ameisenkönigin-Objekt",L"Oggetto della formica regina",L"Figurka królowej mrówek",L"Kraliçe karınca nesnesi",L"قطعة ملكة النمل",L"蚁后摆件",L"여왕개미 장식물",L"Фигурка королевы муравьёв"}, // QueenObject
    {L"Objeto de hormiga rey",L"王蟻のオブジェ",L"Objet du roi fourmi",L"Ameisenkönig-Objekt",L"Oggetto della formica re",L"Figurka króla mrówek",L"Kral karınca nesnesi",L"قطعة ملك النمل",L"蚁王摆件",L"왕개미 장식물",L"Фигурка короля муравьёв"}, // KingObject
    {L"Tarjeta Biohazard",L"バイオハザードカード",L"Carte Biohazard",L"Biohazard-Karte",L"Tessera Biohazard",L"Karta Biohazard",L"Biohazard kartı",L"بطاقة Biohazard",L"生化危害卡",L"바이오해저드 카드",L"Карта Biohazard"}, // BiohazardCard
    {L"Detonador",L"起爆装置",L"Détonateur",L"Zünder",L"Detonatore",L"Detonator",L"Fünye",L"صاعق",L"雷管",L"기폭 장치",L"Детонатор"}, // Detonator
    {L"Palanca de control",L"操作レバー",L"Levier de commande",L"Bedienhebel",L"Leva di controllo",L"Dźwignia sterująca",L"Kontrol kolu",L"ذراع تحكم",L"控制杆",L"조종 레버",L"Рычаг управления"}, // ControlLever
    {L"Libélula dorada",L"金のトンボ",L"Libellule dorée",L"Goldene Libelle",L"Libellula dorata",L"Złota ważka",L"Altın yusufçuk",L"يعسوب ذهبي",L"金蜻蜓",L"황금 잠자리",L"Золотая стрекоза"}, // GoldDragonfly
    {L"Llave de plata",L"銀の鍵",L"Clé en argent",L"Silberschlüssel",L"Chiave d’argento",L"Srebrny klucz",L"Gümüş anahtar",L"مفتاح فضي",L"银钥匙",L"은 열쇠",L"Серебряный ключ"}, // SilverKey
    {L"Llave de oro",L"金の鍵",L"Clé en or",L"Goldschlüssel",L"Chiave d’oro",L"Złoty klucz",L"Altın anahtar",L"مفتاح ذهبي",L"金钥匙",L"금 열쇠",L"Золотой ключ"}, // GoldKey
    {L"Insignia del ejército",L"陸軍の証",L"Insigne de l’armée",L"Heeressiegel",L"Insegna dell’esercito",L"Odznaka wojsk lądowych",L"Kara kuvvetleri nişanı",L"شارة الجيش",L"陆军之证",L"육군 증표",L"Эмблема сухопутных войск"}, // ArmyProof
    {L"Insignia de la armada",L"海軍の証",L"Insigne de la marine",L"Marinesiegel",L"Insegna della marina",L"Odznaka marynarki",L"Deniz kuvvetleri nişanı",L"شارة البحرية",L"海军之证",L"해군 증표",L"Эмблема флота"}, // NavyProof
    {L"Insignia de aviación",L"空軍の証",L"Insigne de l’aviation",L"Luftwaffensiegel",L"Insegna dell’aviazione",L"Odznaka lotnictwa",L"Hava kuvvetleri nişanı",L"شارة القوات الجوية",L"空军之证",L"공군 증표",L"Эмблема авиации"}, // AirProof
    {L"Llave con etiqueta",L"タグ付きの鍵",L"Clé étiquetée",L"Schlüssel mit Anhänger",L"Chiave con etichetta",L"Klucz z etykietą",L"Etiketli anahtar",L"مفتاح ببطاقة",L"带标签的钥匙",L"꼬리표 달린 열쇠",L"Ключ с биркой"}, // TaggedKey
    {L"Tarjeta de identificación",L"IDカード",L"Carte d’identité",L"Ausweiskarte",L"Tessera identificativa",L"Karta identyfikacyjna",L"Kimlik kartı",L"بطاقة هوية",L"身份证卡",L"신분증",L"Удостоверение"}, // IDCard
    {L"Mapa",L"地図",L"Carte",L"Karte",L"Mappa",L"Mapa",L"Harita",L"خريطة",L"地图",L"지도",L"Карта"}, // Map
    {L"Llave del aeropuerto",L"空港の鍵",L"Clé de l’aéroport",L"Flughafenschlüssel",L"Chiave dell’aeroporto",L"Klucz do lotniska",L"Havaalanı anahtarı",L"مفتاح المطار",L"机场钥匙",L"공항 열쇠",L"Ключ от аэропорта"}, // AirportKey
    {L"Tarjeta con emblema",L"エンブレムカード",L"Carte à emblème",L"Emblemkarte",L"Tessera con emblema",L"Karta z emblematem",L"Amblem kartı",L"بطاقة الشعار",L"徽章卡",L"문장 카드",L"Карта с эмблемой"}, // EmblemCard
    {L"Cuadro del esqueleto",L"骸骨の絵",L"Tableau du squelette",L"Skelettbild",L"Quadro dello scheletro",L"Obraz szkieletu",L"İskelet tablosu",L"لوحة الهيكل العظمي",L"骷髅画像",L"해골 그림",L"Картина со скелетом"}, // SkeletonPicture
    {L"Placa de caja de música",L"オルゴールのプレート",L"Plaque de boîte à musique",L"Spieluhrplatte",L"Piastra del carillon",L"Płytka pozytywki",L"Müzik kutusu plakası",L"لوحة صندوق الموسيقى",L"八音盒金属片",L"오르골 판",L"Пластина музыкальной шкатулки"}, // MusicPlate
    {L"Cuerpo",L"胴体",L"Corps",L"Körper",L"Corpo",L"Korpus",L"Gövde",L"جسم",L"躯干",L"몸통",L"Корпус"}, // Body
    {L"Álbum",L"アルバム",L"Album",L"Album",L"Album",L"Album",L"Albüm",L"ألبوم",L"相册",L"앨범",L"Альбом"}, // Album
    {L"Alabarda",L"ハルバード",L"Hallebarde",L"Hellebarde",L"Alabarda",L"Halabarda",L"Teber",L"مطرد",L"戟",L"미늘창",L"Алебарда"}, // Halberd
    {L"Extintor",L"消火器",L"Extincteur",L"Feuerlöscher",L"Estintore",L"Gaśnica",L"Yangın söndürücü",L"مطفأة حريق",L"灭火器",L"소화기",L"Огнетушитель"}, // Extinguisher
    {L"Llave del candado",L"南京錠の鍵",L"Clé du cadenas",L"Vorhängeschlossschlüssel",L"Chiave del lucchetto",L"Klucz do kłódki",L"Asma kilit anahtarı",L"مفتاح القفل",L"挂锁钥匙",L"자물쇠 열쇠",L"Ключ от замка"}, // PadlockKey
    {L"Emblema de aleación especial",L"特殊合金のエンブレム",L"Emblème en alliage spécial",L"Speziallegierungsemblem",L"Emblema in lega speciale",L"Emblemat ze specjalnego stopu",L"Özel alaşım amblemi",L"شعار من سبيكة خاصة",L"特殊合金徽章",L"특수 합금 문장",L"Эмблема из особого сплава"}, // AlloyEmblem
    {L"Manivela de válvula",L"バルブハンドル",L"Poignée de vanne",L"Ventilrad",L"Maniglia della valvola",L"Pokrętło zaworu",L"Vana kolu",L"مقبض صمام",L"阀门把手",L"밸브 핸들",L"Вентиль"}, // Valve
    {L"Manivela octogonal",L"八角形のバルブハンドル",L"Poignée octogonale",L"Achteckiges Ventilrad",L"Maniglia ottagonale",L"Ośmiokątne pokrętło",L"Sekizgen vana kolu",L"مقبض صمام ثماني",L"八角阀门把手",L"팔각 밸브 핸들",L"Восьмигранный вентиль"}, // OctValve
    {L"Llave de sala de máquinas",L"機械室の鍵",L"Clé de la salle des machines",L"Maschinenraumschlüssel",L"Chiave della sala macchine",L"Klucz do maszynowni",L"Makine dairesi anahtarı",L"مفتاح غرفة الآلات",L"机械室钥匙",L"기계실 열쇠",L"Ключ от машинного зала"}, // MachineKey
    {L"Llave de sala de minería",L"採掘室の鍵",L"Clé de la salle d’extraction",L"Bergbauraumschlüssel",L"Chiave della sala mineraria",L"Klucz do pomieszczenia kopalni",L"Maden odası anahtarı",L"مفتاح غرفة التعدين",L"采掘室钥匙",L"채굴실 열쇠",L"Ключ от шахтной комнаты"}, // MiningKey
    {L"Etiqueta de código de barras",L"バーコードシール",L"Étiquette code-barres",L"Strichcodeaufkleber",L"Etichetta con codice a barre",L"Naklejka z kodem kreskowym",L"Barkod etiketi",L"ملصق رمز شريطي",L"条形码标签",L"바코드 스티커",L"Наклейка со штрихкодом"}, // Barcode
    {L"Llave de sala estéril",L"滅菌室の鍵",L"Clé de la salle stérile",L"Sterilraumschlüssel",L"Chiave della sala sterile",L"Klucz do sterylnego pomieszczenia",L"Steril oda anahtarı",L"مفتاح الغرفة المعقمة",L"无菌室钥匙",L"멸균실 열쇠",L"Ключ от стерильной комнаты"}, // SterileKey
    {L"Pomo de puerta",L"ドアノブ",L"Poignée de porte",L"Türknauf",L"Pomello della porta",L"Klamka",L"Kapı kolu",L"مقبض باب",L"门把手",L"문손잡이",L"Дверная ручка"}, // DoorKnob
    {L"Batería",L"バッテリーパック",L"Batterie",L"Batterie",L"Batteria",L"Akumulator",L"Batarya",L"بطارية",L"电池组",L"배터리 팩",L"Аккумулятор"}, // Battery
    {L"Hilo hemostático",L"止血用ワイヤー",L"Fil hémostatique",L"Blutstillungsdraht",L"Filo emostatico",L"Drut hemostatyczny",L"Kan durdurucu tel",L"سلك لإيقاف النزيف",L"止血线",L"지혈용 와이어",L"Кровоостанавливающая проволока"}, // HemostaticWire
    {L"Llave de plataforma giratoria",L"ターンテーブルの鍵",L"Clé de la plateforme tournante",L"Drehscheibenschlüssel",L"Chiave della piattaforma girevole",L"Klucz do obrotnicy",L"Döner platform anahtarı",L"مفتاح المنصة الدوارة",L"转台钥匙",L"회전대 열쇠",L"Ключ от поворотной платформы"}, // TurntableKey
    {L"Llave de almacén químico",L"薬品保管庫の鍵",L"Clé du dépôt chimique",L"Chemielagerschlüssel",L"Chiave del deposito chimico",L"Klucz do magazynu chemicznego",L"Kimyasal depo anahtarı",L"مفتاح مخزن المواد الكيميائية",L"化学品仓库钥匙",L"약품 보관실 열쇠",L"Ключ от химического склада"}, // ChemicalKey
    {L"Figura de tanque",L"戦車のオブジェ",L"Objet en forme de char",L"Panzerobjekt",L"Modellino di carro armato",L"Figurka czołgu",L"Tank nesnesi",L"مجسم دبابة",L"坦克摆件",L"전차 장식물",L"Модель танка"}, // TankObject
    {L"Nota",L"メモ",L"Note",L"Notiz",L"Nota",L"Notatka",L"Not",L"مذكرة",L"备忘录",L"메모",L"Записка"}, // Memo
    {L"Espada oxidada",L"錆びた剣",L"Épée rouillée",L"Rostiges Schwert",L"Spada arrugginita",L"Zardzewiały miecz",L"Paslı kılıç",L"سيف صدئ",L"生锈的剑",L"녹슨 검",L"Ржавый меч"}, // RustedSword
    {L"Medicamento hemostático",L"止血剤",L"Médicament hémostatique",L"Blutstillendes Mittel",L"Medicinale emostatico",L"Lek hemostatyczny",L"Kan durdurucu ilaç",L"دواء لإيقاف النزيف",L"止血药",L"지혈제",L"Кровоостанавливающее средство"}, // Hemostatic
    {L"Tarjeta de seguridad",L"セキュリティカード",L"Carte de sécurité",L"Sicherheitskarte",L"Tessera di sicurezza",L"Karta bezpieczeństwa",L"Güvenlik kartı",L"بطاقة أمنية",L"安全卡",L"보안 카드",L"Карта безопасности"}, // SecurityCard
    {L"Archivo de seguridad",L"セキュリティファイル",L"Dossier de sécurité",L"Sicherheitsakte",L"Documento di sicurezza",L"Akta bezpieczeństwa",L"Güvenlik dosyası",L"ملف أمني",L"安全文件",L"보안 파일",L"Документ безопасности"}, // SecurityFile
    {L"Gargantilla",L"チョーカー",L"Ras-de-cou",L"Halsband",L"Collarino",L"Choker",L"Boyunluk",L"قلادة عنق",L"颈环",L"초커",L"Ожерелье"}, // Choker
    {L"Relieve de hormiga reina",L"女王蟻のレリーフ",L"Relief de la reine fourmi",L"Ameisenkönigin-Relief",L"Rilievo della formica regina",L"Relief królowej mrówek",L"Kraliçe karınca kabartması",L"نقش ملكة النمل",L"蚁后浮雕",L"여왕개미 부조",L"Барельеф королевы муравьёв"}, // QueenRelief
    {L"Relieve de hormiga rey",L"王蟻のレリーフ",L"Relief du roi fourmi",L"Ameisenkönig-Relief",L"Rilievo della formica re",L"Relief króla mrówek",L"Kral karınca kabartması",L"نقش ملك النمل",L"蚁王浮雕",L"왕개미 부조",L"Барельеф короля муравьёв"}, // KingRelief
    {L"Joya roja",L"赤い宝石",L"Joyau rouge",L"Rotes Juwel",L"Gioiello rosso",L"Czerwony klejnot",L"Kırmızı mücevher",L"جوهرة حمراء",L"红宝石",L"붉은 보석",L"Красная драгоценность"}, // RedJewel
    {L"Joya azul",L"青い宝石",L"Joyau bleu",L"Blaues Juwel",L"Gioiello blu",L"Niebieski klejnot",L"Mavi mücevher",L"جوهرة زرقاء",L"蓝宝石",L"파란 보석",L"Синяя драгоценность"}, // BlueJewel
    {L"Casquillo",L"ソケット",L"Douille",L"Stecknuss",L"Bussola",L"Nasadka",L"Lokma",L"رأس مفتاح",L"套筒",L"소켓",L"Торцевая головка"}, // Socket
    {L"Manivela cuadrada",L"四角形のバルブハンドル",L"Poignée carrée",L"Vierkantiges Ventilrad",L"Maniglia quadrata",L"Kwadratowe pokrętło",L"Kare vana kolu",L"مقبض صمام مربع",L"方形阀门把手",L"사각 밸브 핸들",L"Квадратный вентиль"}, // SquareValve
    {L"Suero",L"血清",L"Sérum",L"Serum",L"Siero",L"Surowica",L"Serum",L"مصل",L"血清",L"혈청",L"Сыворотка"}, // Serum
    {L"Jarrón de cerámica",L"陶器の壺",L"Vase en céramique",L"Tonvase",L"Vaso di ceramica",L"Ceramiczny wazon",L"Seramik vazo",L"مزهرية فخارية",L"陶制花瓶",L"도자기 화병",L"Керамическая ваза"}, // Vase
    {L"Pisapapeles",L"ペーパーウェイト",L"Presse-papier",L"Briefbeschwerer",L"Fermacarte",L"Przycisk do papieru",L"Kâğıt ağırlığı",L"ثقالة ورق",L"镇纸",L"문진",L"Пресс-папье"}, // Paperweight
    {L"Libélula plateada",L"銀のトンボ",L"Libellule argentée",L"Silberne Libelle",L"Libellula argentata",L"Srebrna ważka",L"Gümüş yusufçuk",L"يعسوب فضي",L"银蜻蜓",L"은 잠자리",L"Серебряная стрекоза"}, // SilverDragonfly
    {L"Ala",L"羽",L"Aile",L"Flügel",L"Ala",L"Skrzydło",L"Kanat",L"جناح",L"翅膀",L"날개",L"Крыло"}, // Wing
    {L"Cristal",L"クリスタル",L"Cristal",L"Kristall",L"Cristallo",L"Kryształ",L"Kristal",L"بلورة",L"水晶",L"수정",L"Кристалл"}, // Crystal
    {L"Archivo",L"ファイル",L"Dossier",L"Akte",L"Documento",L"Akta",L"Dosya",L"ملف",L"文件",L"파일",L"Документ"}, // File
    {L"Maceta",L"植木鉢",L"Pot de fleurs",L"Blumentopf",L"Vaso per piante",L"Doniczka",L"Saksı",L"وعاء نبات",L"花盆",L"화분",L"Цветочный горшок"}, // PlantPot
    {L"Fotografía",L"写真",L"Photo",L"Foto",L"Fotografia",L"Zdjęcie",L"Fotoğraf",L"صورة",L"照片",L"사진",L"Фотография"}, // Picture
    {L"Pistola mejorada",L"強化ハンドガン",L"Pistolet amélioré",L"Verbesserte Pistole",L"Pistola potenziata",L"Ulepszony pistolet",L"Geliştirilmiş tabanca",L"مسدس محسن",L"强化手枪",L"강화 권총",L"Улучшенный пистолет"}, // EnhancedHandgun
    {L"Recorte",L"切り抜き",L"Coupure",L"Ausschnitt",L"Ritaglio",L"Wycinek",L"Kupür",L"قصاصة",L"剪报",L"스크랩",L"Вырезка"}, // BoardClip
    {L"Tarjeta",L"カード",L"Carte",L"Karte",L"Tessera",L"Karta",L"Kart",L"بطاقة",L"卡片",L"카드",L"Карточка"}, // Card
    {L"Recorte de periódico",L"新聞の切り抜き",L"Coupure de journal",L"Zeitungsausschnitt",L"Ritaglio di giornale",L"Wycinek z gazety",L"Gazete kupürü",L"قصاصة صحيفة",L"报纸剪报",L"신문 스크랩",L"Газетная вырезка"}, // Newspaper
    {L"Réplica de Luger",L"ルガーのレプリカ",L"Réplique de Luger",L"Luger-Nachbildung",L"Replica di Luger",L"Replika Lugera",L"Luger replikası",L"نسخة لوغر",L"鲁格手枪复制品",L"루거 모형",L"Копия люгера"}, // LugerReplica
    {L"Completo",L"完成",L"Complet",L"Vollständig",L"Completo",L"Kompletny",L"Tamamlanmış",L"مكتمل",L"完整",L"완성",L"В сборе"}, // Complete
    {L"Foto familiar",L"家族写真",L"Photo de famille",L"Familienfoto",L"Foto di famiglia",L"Zdjęcie rodzinne",L"Aile fotoğrafı",L"صورة عائلية",L"全家福",L"가족사진",L"Семейная фотография"}, // FamilyPicture
    {L"Carpetas de archivos",L"ファイルフォルダー",L"Chemises de dossiers",L"Aktenordner",L"Cartelle di documenti",L"Teczki na akta",L"Dosya klasörleri",L"مجلدات ملفات",L"文件夹",L"서류철",L"Папки с документами"}, // Folders
    {L"Mando a distancia",L"リモコン",L"Télécommande",L"Fernbedienung",L"Telecomando",L"Pilot",L"Uzaktan kumanda",L"جهاز تحكم عن بعد",L"遥控器",L"리모컨",L"Пульт управления"}, // Remote
    {L"Mezcla",L"混合液",L"Mélange",L"Mischung",L"Miscela",L"Mieszanka",L"Karışım",L"خليط",L"混合液",L"혼합액",L"Смесь"}, // Mixture
    {L"Manual de juego",L"プレイングマニュアル",L"Manuel de jeu",L"Spielanleitung",L"Manuale di gioco",L"Instrukcja gry",L"Oyun kılavuzu",L"دليل اللعب",L"游戏手册",L"게임 설명서",L"Руководство по игре"}, // Manual
    {L"Vacío",L"空",L"Vide",L"Leer",L"Vuoto",L"Puste",L"Boş",L"فارغ",L"空",L"비어 있음",L"Пусто"}, // Empty
    {L"Objeto desconocido",L"不明なアイテム",L"Objet inconnu",L"Unbekannter Gegenstand",L"Oggetto sconosciuto",L"Nieznany przedmiot",L"Bilinmeyen eşya",L"عنصر غير معروف",L"未知物品",L"알 수 없는 아이템",L"Неизвестный предмет"}, // Unknown
    {L"Casquillo cuadrado",L"四角いソケット",L"Douille carrée",L"Vierkant-Stecknuss",L"Bussola quadrata",L"Kwadratowa nasadka",L"Kare lokma",L"رأس مفتاح مربع",L"方形套筒",L"사각 소켓",L"Квадратная торцевая головка"}, // SquareSocket
    {L"Llave de emblema",L"紋章の鍵",L"Clé à blason",L"Wappenschlüssel",L"Chiave con stemma",L"Klucz z herbem",L"Arma anahtarı",L"مفتاح شعار",L"纹章钥匙",L"문장 열쇠",L"Ключ с гербом"}, // CrestKey
    {L"Zombi",L"ゾンビ",L"Zombie",L"Zombie",L"Zombi",L"Zombie",L"Zombi",L"زومبي",L"丧尸",L"좀비",L"Зомби"}, // Zombie
    {L"Araña",L"クモ",L"Araignée",L"Spinne",L"Ragno",L"Pająk",L"Örümcek",L"عنكبوت",L"蜘蛛",L"거미",L"Паук"}, // Spider
    {L"Perro zombi",L"ゾンビ犬",L"Chien zombie",L"Zombiehund",L"Cane zombi",L"Pies zombie",L"Zombi köpek",L"كلب زومبي",L"丧尸犬",L"좀비견",L"Зомби-пёс"}, // ZombieDog
    {L"Polilla",L"蛾",L"Papillon de nuit",L"Motte",L"Falena",L"Ćma",L"Güve",L"عثة",L"飞蛾",L"나방",L"Мотылёк"}, // Moth
    {L"Murciélago",L"コウモリ",L"Chauve-souris",L"Fledermaus",L"Pipistrello",L"Nietoperz",L"Yarasa",L"خفاش",L"蝙蝠",L"박쥐",L"Летучая мышь"}, // Bat
    {L"Steve mutado",L"変異スティーブ",L"Steve muté",L"Mutierter Steve",L"Steve mutato",L"Zmutowany Steve",L"Mutasyona uğramış Steve",L"ستيف المتحول",L"变异史蒂夫",L"변이 스티브",L"Мутировавший Стив"}, // MutatedSteve
    {L"Araña gigante",L"巨大グモ",L"Araignée géante",L"Riesenspinne",L"Ragno gigante",L"Olbrzymi pająk",L"Dev örümcek",L"عنكبوت عملاق",L"巨型蜘蛛",L"거대 거미",L"Гигантский паук"}, // GiantSpider
    {L"Zombi anatomista",L"解剖学者ゾンビ",L"Zombie anatomiste",L"Anatomie-Zombie",L"Zombi anatomista",L"Zombie anatom",L"Anatomist zombi",L"زومبي عالم التشريح",L"解剖学家丧尸",L"해부학자 좀비",L"Зомби-анатом"}, // Anatomist
    {L"Tentáculo",L"触手",L"Tentacule",L"Tentakel",L"Tentacolo",L"Macka",L"Dokunaç",L"مجس",L"触手",L"촉수",L"Щупальце"}, // Tentacle
    {L"Enemigo",L"敵",L"Ennemi",L"Gegner",L"Nemico",L"Wróg",L"Düşman",L"عدو",L"敌人",L"적",L"Враг"}, // Enemy
    {L"Munición",L"弾薬",L"Munitions",L"Munition",L"Munizioni",L"Amunicja",L"Mühimmat",L"ذخيرة",L"弹药",L"탄약",L"Боеприпасы"}, // Ammo
};
const wchar_t* tr(Term term,Language lang) {return terms[term][std::clamp(int(lang)-2,0,10)];}
}

Language language(const char* code) {
    if(!code)return EN;
    for(int i=0;i<COUNT;i++)if(!std::strcmp(code,k_lang_id[i]))return Language(i);
    if(!std::strncmp(code,"pt",2))return PT;if(!std::strncmp(code,"zh",2))return ZH;
    for(int i=0;i<COUNT;i++)if(!std::strncmp(code,k_lang_id[i],2))return Language(i);
    return EN;
}
Language selection(int saved,int launcher_language) {
    // Preserve previously saved choices: 0 follow, 1 English, 2 Portuguese.
    if(saved==1)return EN;if(saved==2)return PT;
    if(saved>=3&&saved<=COUNT)return Language(saved-1);
    return Language(std::clamp(launcher_language,0,int(COUNT)-1));
}
const wchar_t* font_face(Language lang,bool bold) {
    switch(lang) {
    case JA:return L"Yu Gothic UI";case ZH:return L"Microsoft YaHei UI";
    case KO:return L"Malgun Gothic";default:return bold?L"Segoe UI Semibold":L"Segoe UI";
    }
}
bool rtl_text(const std::wstring& text) {
    for(wchar_t c:text)if((c>=0x0600&&c<=0x08ff)||(c>=0xfb50&&c<=0xfdff)||(c>=0xfe70&&c<=0xfeff))return true;
    return false;
}
std::array<std::wstring,17> labels(Language lang) {
    static_assert(COUNT==LANG_COUNT&&PT==LANG_PT&&RU==LANG_RU,"Shared launcher language order");
    const int ids[]={S_SRT_HEALTH,S_SRT_TIME,S_SRT_SAVE,S_SRT_RETRY,S_SRT_ENEMIES,S_SRT_INVENTORY,S_SRT_EQUIPPED,
        S_SRT_FINE,S_SRT_CAUTION,S_SRT_DANGER,S_SRT_POISON,S_SRT_GAS,S_SRT_DEAD,S_SRT_WAIT,S_SRT_EMPTY,S_SRT_BOSS,S_SRT_PHASE};
    std::array<std::wstring,17> result;for(int i=0;i<17;i++)result[i]=k_str[lang][ids[i]];return result;
}
std::wstring credit(Language lang) {return srt_credit_text(lang);}
std::wstring quantity(const cv_srt_item& item) {
    if(!item.id||!item.counted)return L"";
    if(item.infinite)return L"∞";
    // Percent-based magazines: assault rifle, twin SMGs and M1P.
    if(item.id==2||item.id==0x22||item.id==0x8e)return std::to_wstring(item.quantity)+L"%";
    return L"× "+std::to_wstring(item.quantity);
}
std::wstring item(int id,Language lang) {
    if(lang==PT||lang==EN)return original_item(id,lang==PT);
    auto t=[&](Term term)->std::wstring {return tr(term,lang);};
    auto with=[&](Term term,const std::wstring& object) {return t(term)+L" ("+object+L")";};
    if(id==0)return t(Empty);
    if(id>=0x18&&id<=0x1d) {
        static const Term colors[][3]={{Green,Green,Green},{Green,Red,Red},{Green,Blue,Blue},{Green,Green,Blue},{Green,Green,Green},{Green,Red,Blue}};
        const int n=id>=0x1b?3:2;std::wstring result=t(MixedHerbs)+L" (";
        for(int i=0;i<n;i++) {if(i)result+=L"+";result+=t(colors[id-0x18][i]);}return result+L")";
    }
    switch(id) {
    case 5:return L"Glock 17";case 0x20:return L"Magnum";case 0x55:return L"TG-01";
    case 0x62:return L"Clement Alpha";case 0x63:return L"Clement Sigma";case 0x8e:return L"M1P";
    case 0x13:return with(Parts,L"M93R");
    case 0x2b:return with(Pierce,L"Alexander");case 0x2c:return with(Jewel,L"Alexander");
    case 0x2d:return with(Ring,L"Alfred");case 0x2e:return with(Jewel,L"Alfred");
    case 0x3f:return with(Case,with(Parts,L"M93R"));
    case 0x4f:return with(GoldDragonfly,t(Body));
    case 0x66:return with(Memo,L"Alfred");case 0x6b:return with(Choker,L"Alexia");case 0x6c:return with(Jewel,L"Alexia");
    case 0x76:return with(SilverDragonfly,t(Body));
    case 0x7a:case 0x7b:case 0x7c:return with(GoldDragonfly,std::to_wstring(id-0x79)+L" × "+t(Wing));
    case 0x7f:return with(Picture,L"B");case 0x80:return with(Case,t(BowPowder));case 0x81:return with(Case,t(MagnumAmmo));
    case 0x89:return with(QueenRelief,t(Complete));case 0x8f:return t(Ammo)+L" (Calico)";
    case 0x90:return with(Mixture,L"Clement");case 0x95:return with(Extinguisher,t(Empty));
    case 0x98:return with(CrestKey,L"S");case 0x99:return with(CrestKey,L"G");
    case 0x01:return t(Rocket);
    case 0x02:return t(Assault);
    case 0x03:return t(Sniper);
    case 0x04:return t(Shotgun);
    case 0x06:return t(GrenadeLauncher);
    case 0x07:return t(BowGun);
    case 0x08:return t(Knife);
    case 0x09:return t(Handgun);
    case 0x0a:return t(CustomHandgun);
    case 0x0b:return t(LinearLauncher);
    case 0x0c:return t(HandgunAmmo);
    case 0x0d:return t(MagnumAmmo);
    case 0x0e:return t(Shells);
    case 0x0f:return t(Grenades);
    case 0x10:return t(Acid);
    case 0x11:return t(Flame);
    case 0x12:return t(Arrows);
    case 0x14:return t(FirstAid);
    case 0x15:return t(GreenHerb);
    case 0x16:return t(RedHerb);
    case 0x17:return t(BlueHerb);
    case 0x1e:return t(MagnumAmmo);
    case 0x1f:return t(InkRibbon);
    case 0x21:return t(GoldLugers);
    case 0x22:return t(SMGs);
    case 0x23:return t(BowPowder);
    case 0x24:return t(ExplosiveArrows);
    case 0x25:return t(BOWGas);
    case 0x26:return t(MGAmmo);
    case 0x27:return t(GasMask);
    case 0x28:return t(RifleAmmo);
    case 0x29:return t(DuraluminCase);
    case 0x2a:return t(AssaultAmmo);
    case 0x2f:return t(PrisonerDiary);
    case 0x30:return t(DirectorMemo);
    case 0x31:return t(Instructions);
    case 0x32:return t(Lockpick);
    case 0x33:return t(GlassEye);
    case 0x34:return t(PianoRoll);
    case 0x35:return t(Wheel);
    case 0x36:return t(CraneKey);
    case 0x37:return t(Lighter);
    case 0x38:return t(EaglePlate);
    case 0x39:return t(SidePack);
    case 0x3a:return t(MapRoll);
    case 0x3b:return t(HawkEmblem);
    case 0x3c:return t(QueenObject);
    case 0x3d:return t(KingObject);
    case 0x3e:return t(BiohazardCard);
    case 0x40:return t(Detonator);
    case 0x41:return t(ControlLever);
    case 0x42:return t(GoldDragonfly);
    case 0x43:return t(SilverKey);
    case 0x44:return t(GoldKey);
    case 0x45:return t(ArmyProof);
    case 0x46:return t(NavyProof);
    case 0x47:return t(AirProof);
    case 0x48:return t(TaggedKey);
    case 0x49:return t(IDCard);
    case 0x4a:return t(Map);
    case 0x4b:return t(AirportKey);
    case 0x4c:return t(EmblemCard);
    case 0x4d:return t(SkeletonPicture);
    case 0x4e:return t(MusicPlate);
    case 0x50:return t(Album);
    case 0x51:return t(Halberd);
    case 0x52:return t(Extinguisher);
    case 0x53:return t(Case);
    case 0x54:return t(PadlockKey);
    case 0x56:return t(AlloyEmblem);
    case 0x57:return t(Valve);
    case 0x58:return t(OctValve);
    case 0x59:return t(MachineKey);
    case 0x5a:return t(MiningKey);
    case 0x5b:return t(Barcode);
    case 0x5c:return t(SterileKey);
    case 0x5d:return t(DoorKnob);
    case 0x5e:return t(Battery);
    case 0x5f:return t(HemostaticWire);
    case 0x60:return t(TurntableKey);
    case 0x61:return t(ChemicalKey);
    case 0x64:return t(TankObject);
    case 0x65:return t(AlloyEmblem);
    case 0x67:return t(RustedSword);
    case 0x68:return t(Hemostatic);
    case 0x69:return t(SecurityCard);
    case 0x6a:return t(SecurityFile);
    case 0x6d:return t(QueenRelief);
    case 0x6e:return t(KingRelief);
    case 0x6f:return t(RedJewel);
    case 0x70:return t(BlueJewel);
    case 0x71:return t(Socket);
    case 0x72:return t(SquareValve);
    case 0x73:return t(Serum);
    case 0x74:return t(Vase);
    case 0x75:return t(Paperweight);
    case 0x77:return t(SilverDragonfly);
    case 0x78:return t(Wing);
    case 0x79:return t(Crystal);
    case 0x7d:return t(File);
    case 0x7e:return t(PlantPot);
    case 0x82:return t(BowPowder);
    case 0x83:return t(EnhancedHandgun);
    case 0x84:return t(Memo);
    case 0x85:return t(BoardClip);
    case 0x86:return t(Card);
    case 0x87:return t(Newspaper);
    case 0x88:return t(LugerReplica);
    case 0x8a:return t(FamilyPicture);
    case 0x8b:return t(Folders);
    case 0x8c:return t(Remote);
    case 0x91:return t(Manual);
    case 0x96:return t(SquareSocket);
    default: {wchar_t hex[16];swprintf(hex,16,L"%02X",id);return with(Unknown,hex);}
    }
}
std::wstring enemy(int id,Language lang) {
    // Proper names/designations remain identical in every SRT language.
    switch(id) {
    case 5:return L"Hunter";case 9:return L"Bandersnatch";
    case 12:return L"Alexia Ashford";case 13:return L"Alexia — II";case 14:return L"Alexia — III";
    case 15:return L"Nosferatu";case 21:case 22:return L"Albinoid";case 30:return L"Alexia Baby";
    }
    // Descriptions follow the SRT language, shared by the list and boss bar.
    struct Name {int id;const wchar_t* text[COUNT];};
    static const Name names[]={
        {1,{L"Zumbi",L"Zombie",L"Zombi",L"ゾンビ",L"Zombie",L"Zombie",L"Zombi",L"Zombie",L"Zombi",L"زومبي",L"丧尸",L"좀비",L"Зомби"}},
        {2,{L"Verme gigante",L"Gulp Worm",L"Gusano gigante",L"巨大ワーム",L"Ver géant",L"Riesenwurm",L"Verme gigante",L"Olbrzymi robak",L"Dev solucan",L"دودة عملاقة",L"巨型蠕虫",L"거대 벌레",L"Гигантский червь"}},
        {3,{L"Viúva-negra",L"Black Widow",L"Viuda negra",L"クロゴケグモ",L"Veuve noire",L"Schwarze Witwe",L"Vedova nera",L"Czarna wdowa",L"Karadul",L"الأرملة السوداء",L"黑寡妇蜘蛛",L"검은과부거미",L"Чёрная вдова"}},
        {4,{L"Cão zumbi",L"Zombie Dog",L"Perro zombi",L"ゾンビ犬",L"Chien zombie",L"Zombiehund",L"Cane zombi",L"Pies zombie",L"Zombi köpek",L"كلب زومبي",L"丧尸犬",L"좀비견",L"Зомби-пёс"}},
        {6,{L"Mariposa",L"Moth",L"Polilla",L"蛾",L"Papillon de nuit",L"Motte",L"Falena",L"Ćma",L"Güve",L"عثة",L"飞蛾",L"나방",L"Мотылёк"}},
        {7,{L"Morcego",L"Bat",L"Murciélago",L"コウモリ",L"Chauve-souris",L"Fledermaus",L"Pipistrello",L"Nietoperz",L"Yarasa",L"خفاش",L"蝙蝠",L"박쥐",L"Летучая мышь"}},
        {17,{L"Steve mutante",L"Mutated Steve",L"Steve mutado",L"変異したSteve",L"Steve muté",L"Mutierter Steve",L"Steve mutato",L"Zmutowany Steve",L"Mutasyona uğramış Steve",L"Steve المتحول",L"变异的Steve",L"변이한 Steve",L"Мутировавший Steve"}},
        {19,{L"Tirano",L"Tyrant",L"Tirano",L"暴君",L"Tyran",L"Tyrann",L"Tiranno",L"Tyran",L"Tiran",L"الطاغية",L"暴君",L"폭군",L"Тиран"}},
        {23,{L"Viúva-negra gigante",L"Giant Black Widow",L"Viuda negra gigante",L"巨大クロゴケグモ",L"Veuve noire géante",L"Riesige Schwarze Witwe",L"Vedova nera gigante",L"Olbrzymia czarna wdowa",L"Dev karadul",L"الأرملة السوداء العملاقة",L"巨型黑寡妇蜘蛛",L"거대 검은과부거미",L"Гигантская чёрная вдова"}},
        {26,{L"Zumbi anatomista",L"Anatomist Zombie",L"Zombi anatomista",L"解剖学者ゾンビ",L"Zombie anatomiste",L"Anatomie-Zombie",L"Zombi anatomista",L"Zombie anatom",L"Anatomist zombi",L"زومبي عالم التشريح",L"解剖学家丧尸",L"해부학자 좀비",L"Зомби-анатом"}},
        {29,{L"Tentáculo",L"Tentacle",L"Tentáculo",L"触手",L"Tentacule",L"Tentakel",L"Tentacolo",L"Macka",L"Dokunaç",L"مجس",L"触手",L"촉수",L"Щупальце"}},
    };
    const auto selected=Language(std::clamp(int(lang),0,int(COUNT)-1));
    for(const auto& name:names)if(name.id==id)return name.text[selected];
    return selected==PT?L"Inimigo":selected==EN?L"Enemy":tr(Enemy,selected);
}
}
