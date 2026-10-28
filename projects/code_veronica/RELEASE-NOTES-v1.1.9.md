# v1.1.9 — HUD, equipment shortcuts and shader compatibility

## Português

- HUD de combate opcional com vida, estado de saúde, arma, munição carregada e reserva. Inclui isqueiro, animação de dano, brilho na vida, veneno/gás, escala por resolução e fade após cinco segundos sem ação. Texto e cores usam os limites do inventário original (120/60/30 HP).
- Recarga manual opcional sem abrir o inventário: mire e pressione o comando configurado. Teclado e controle podem ser usados juntos. Depende de munição compatível e das animações nativas; armas sem recarga compatível continuam seguindo as regras do jogo.
- Quatro atalhos de equipamento na gameplay, com configuração e edição dentro do jogo. Segure L1, escolha a direção com o analógico direito; Triângulo edita e X confirma. No teclado: V, I/J/K/L, F e Enter. Todos os comandos são configuráveis na nova aba HUD. Armas já atribuídas trocam de lugar; um espaço vazio permite mover. Só usa equipamentos carregados, sem acessar o baú ou criar itens. Soltar durante a edição cancela; soltar após confirmar equipa e fecha.
- Escolha DXC/FXC em Desempenho → Compilador de shaders. DXC permanece padrão; FXC é uma alternativa para máquinas com texturas ausentes ou incorretas, confirmada no teste da issue #18. A mudança exige reiniciar e os caches são separados. Não é uma garantia de corrigir todo problema gráfico.
- HUD, recarga manual e atalhos são independentes e desligados por padrão. Interface, dicas e notas nos 13 idiomas. Mantidas tradução PT-BR EU/US, sombras dinâmicas, troféus, personagens, saves e correções anteriores. Filtragem anisotrópica forçada continua desativada; granulado ainda exige reiniciar.
- Extraia o ZIP completo e mantenha Code Veronica.exe, bin/ e assets/ juntos. Não inclui jogo, modelos 3D do jogo, saves ou configurações pessoais. Veronica PKG Extractor v1.0.1 permanece em ZIP separado.

## English

- Optional combat HUD: health, condition, weapon, loaded/reserve ammo and lighter icon, damage trail, moving health highlight, poison/gas colors, resolution scaling and five-second idle fade. Health text and colors now match the original inventory thresholds (120/60/30 HP).
- Optional manual reload without the inventory: aim and press your assigned command. Keyboard and controller work together. Compatible ammunition and native animation rules still apply; weapons without a supported reload retain their original behavior.
- Four live equipment shortcuts, editable in game. Hold L1 and use the right stick; Triangle edits and Cross confirms. Keyboard defaults: V, I/J/K/L, F and Enter. Remap every command in the new HUD tab. Already assigned weapons swap slots; empty slots allow moving. Only carried equipment is available. Releasing while editing cancels; releasing after confirmation equips and closes.
- Choose DXC/FXC under Performance → Shader compiler. DXC remains the default; FXC is a compatibility alternative for missing/incorrect textures, confirmed in issue #18 testing. Restart required; separate caches. It is not a universal graphics fix.
- HUD, manual reload and equipment shortcuts are independent and off by default. UI, hints and notes in all 13 languages. Existing EU/US PT-BR, shadows, trophies, characters, saves and fixes retained. Forced anisotropy stays disabled; film grain still requires restart.
- Extract the complete ZIP; keep Code Veronica.exe, bin/ and assets/ together. No game files, game 3D models, saves or personal settings included. Veronica PKG Extractor v1.0.1 is a separate ZIP.

## Español

- HUD opcional con vida, estado, arma, munición y encendedor; animación de daño, brillo, veneno/gas, escala por resolución y desaparición tras cinco segundos. Estados sincronizados con el inventario: 120/60/30 HP.
- Recarga manual opcional sin inventario: apunta y pulsa el comando asignado. Teclado y mando simultáneos; conserva las reglas de munición y animación del juego.
- Cuatro accesos de equipo editables durante el juego. Mantén L1, elige con stick derecho, Triángulo edita y X confirma. Teclado: V, I/J/K/L, F, Enter. Comandos configurables en HUD. Intercambia posiciones o mueve a un espacio vacío; solo equipo transportado. Soltar durante la edición cancela; después de confirmar equipa y cierra.
- Selector DXC/FXC en Rendimiento. DXC predeterminado; FXC ofrece compatibilidad para texturas ausentes/incorrectas, comprobada en la incidencia #18. Requiere reiniciar, con cachés independientes.
- Funciones independientes, desactivadas por defecto; interfaz en 13 idiomas. Se conservan PT-BR EU/US, sombras, trofeos, personajes, partidas y correcciones. Anisotropía forzada desactivada; grano requiere reiniciar. Extrae el ZIP completo con bin/ y assets/. Extractor v1.0.1 separado; sin datos del juego ni ajustes personales.

## 日本語

- 任意の戦闘HUDに体力、状態、武器、装填弾・予備弾、ライターを表示。ダメージ残像、移動する光、毒・ガス、解像度対応、5秒後のフェードを追加。状態の境界は元のインベントリと同じ120/60/30 HP。
- インベントリを開かず任意の手動リロード。構えて指定キーを押します。キーボードとコントローラーを併用可能。弾薬とアニメーションの元の制限を維持します。
- ゲーム中に編集可能な4方向の装備ショートカット。L1長押し＋右スティック、△で編集、×で決定。キーボードはV、I/J/K/L、F、Enter。HUDタブで変更可能。登録済み装備は位置交換、空き枠へ移動。所持品のみ使用。編集中に離すと取消、決定後に離すと装備して閉じます。
- パフォーマンスにDXC/FXC選択を追加。標準はDXC。FXCはテクスチャ不具合向け互換設定（問題#18で確認）。再起動が必要でキャッシュは独立。
- 各新機能は個別に無効が初期値。13言語対応。既存の翻訳、影、トロフィー、キャラクター、セーブ、修正を維持。強制異方性フィルターは無効、粒子設定は再起動が必要。ZIP全体を展開しbin/とassets/を保持。Extractor v1.0.1は別ZIP。ゲームデータや個人設定は含みません。

## Français

- HUD de combat facultatif : vie, état, arme, munitions et briquet, animation de dégâts, reflet mobile, poison/gaz, résolution adaptative et fondu après cinq secondes. Seuils identiques à l’inventaire : 120/60/30 PV.
- Recharge manuelle facultative sans inventaire : visez puis appuyez sur la commande choisie. Clavier et manette ensemble ; règles natives de munitions et d’animation conservées.
- Quatre raccourcis d’équipement modifiables en jeu. Maintenez L1, sélectionnez au stick droit ; Triangle modifie, Croix confirme. Clavier : V, I/J/K/L, F, Entrée. Remappage dans HUD. Échange des places ou déplacement vers un emplacement vide ; équipement porté uniquement. Relâcher pendant l’édition annule, après confirmation équipe et ferme.
- Choix DXC/FXC dans Performances. DXC par défaut ; FXC est une alternative de compatibilité pour les textures absentes/incorrectes, confirmée dans le test #18. Redémarrage requis et caches séparés.
- Fonctions indépendantes, désactivées par défaut ; 13 langues. Traductions EU/US, ombres, trophées, personnages, sauvegardes et corrections conservés. Anisotropie forcée désactivée ; grain au redémarrage. Extraire tout le ZIP avec bin/ et assets/. Extractor v1.0.1 séparé ; aucun fichier du jeu ni réglage personnel.

## Deutsch

- Optionales Kampf-HUD mit Leben, Zustand, Waffe, Munition und Feuerzeug; Schadensspur, wanderndem Glanz, Gift/Gas, Auflösungsskalierung und Ausblenden nach fünf Sekunden. Zustandsgrenzen entsprechen dem Inventar: 120/60/30 HP.
- Optional ohne Inventar nachladen: zielen und zugewiesene Taste drücken. Tastatur und Controller gemeinsam nutzbar. Native Munitions- und Animationsregeln bleiben erhalten.
- Vier im Spiel bearbeitbare Ausrüstungsplätze. L1 halten, rechten Stick verwenden; Dreieck bearbeitet, Kreuz bestätigt. Tastatur: V, I/J/K/L, F, Eingabe. Im HUD-Tab frei belegbar. Zugewiesene Waffen tauschen Plätze; leere Plätze erlauben Verschieben. Nur mitgeführte Gegenstände. Loslassen beim Bearbeiten bricht ab, nach Bestätigung wird ausgerüstet und geschlossen.
- DXC/FXC-Auswahl unter Leistung. DXC bleibt Standard; FXC ist eine Kompatibilitätsalternative bei fehlenden/falschen Texturen, in Problem #18 bestätigt. Neustart nötig, getrennte Caches.
- Neue Funktionen unabhängig und standardmäßig aus; 13 Sprachen. Bisherige Übersetzungen, Schatten, Trophäen, Figuren, Spielstände und Korrekturen bleiben. Erzwungene Anisotropie bleibt aus; Filmkorn benötigt Neustart. Gesamtes ZIP mit bin/ und assets/ entpacken. Extractor v1.0.1 separat; keine Spieldaten oder persönlichen Einstellungen.

## Italiano

- HUD opzionale con vita, stato, arma, munizioni e accendino; scia danni, luce mobile, veleno/gas, scala per risoluzione e dissolvenza dopo cinque secondi. Soglie dell’inventario originale: 120/60/30 HP.
- Ricarica manuale opzionale senza inventario: mira e premi il comando assegnato. Tastiera e controller insieme; regole native di munizioni e animazioni conservate.
- Quattro scorciatoie modificabili in gioco. Tieni L1 e usa la levetta destra; Triangolo modifica, Croce conferma. Tastiera: V, I/J/K/L, F, Invio. Rimappabili nella scheda HUD. Scambio tra slot o spostamento in uno vuoto; solo oggetti trasportati. Rilasciare durante la modifica annulla, dopo la conferma equipaggia e chiude.
- Scelta DXC/FXC in Prestazioni. DXC predefinito; FXC alternativo per texture mancanti/errate, confermato nel test #18. Richiede riavvio; cache separate.
- Funzioni indipendenti e disattivate inizialmente; 13 lingue. Traduzioni, ombre, trofei, personaggi, salvataggi e correzioni mantenuti. Anisotropia forzata disattivata; grana richiede riavvio. Estrai tutto il ZIP con bin/ e assets/. Extractor v1.0.1 separato; niente dati di gioco o impostazioni personali.

## Polski

- Opcjonalny HUD: życie, stan, broń, amunicja i zapalniczka; ślad obrażeń, ruchomy blask, trucizna/gaz, skalowanie rozdzielczości i zanikanie po pięciu sekundach. Progi jak w oryginalnym ekwipunku: 120/60/30 HP.
- Opcjonalne ręczne przeładowanie bez ekwipunku: wyceluj i naciśnij przypisany przycisk. Klawiatura i kontroler jednocześnie; zachowane zasady amunicji i animacji.
- Cztery skróty wyposażenia edytowane w grze. Przytrzymaj L1, wybierz prawym drążkiem; Trójkąt edytuje, Krzyżyk zatwierdza. Klawiatura: V, I/J/K/L, F, Enter. Zmiana przypisań w HUD. Zamiana miejsc lub przenoszenie do pustego; tylko noszony sprzęt. Zwolnienie podczas edycji anuluje, po zatwierdzeniu wyposaża i zamyka.
- Wybór DXC/FXC w Wydajności. DXC domyślny; FXC alternatywą dla brakujących/błędnych tekstur, potwierdzoną w zgłoszeniu #18. Wymaga restartu, osobne pamięci podręczne.
- Nowe funkcje niezależne i domyślnie wyłączone; 13 języków. Dotychczasowe tłumaczenia, cienie, trofea, postacie, zapisy i poprawki zachowane. Wymuszona anizotropia wyłączona; ziarno wymaga restartu. Rozpakuj cały ZIP z bin/ i assets/. Extractor v1.0.1 osobno; bez danych gry i ustawień osobistych.

## Türkçe

- İsteğe bağlı HUD: can, durum, silah, mühimmat ve çakmak; hasar izi, hareketli parlama, zehir/gaz, çözünürlük ölçekleme ve beş saniye sonra solma. Durum eşikleri özgün envanterle aynı: 120/60/30 HP.
- Envantere girmeden isteğe bağlı elle doldurma: nişan alın ve atanan tuşa basın. Klavye ve kontrolcü birlikte çalışır; özgün mühimmat ve animasyon kuralları korunur.
- Oyun içinde düzenlenebilen dört ekipman kısayolu. L1 basılı, sağ çubukla seç; Üçgen düzenler, Çarpı onaylar. Klavye: V, I/J/K/L, F, Enter. HUD sekmesinden değiştirilebilir. Atanmış silahlar yer değiştirir, boş yuvaya taşınabilir. Yalnızca taşınan eşyalar. Düzenlerken bırakmak iptal eder, onaydan sonra bırakmak kuşanıp kapatır.
- Performans altında DXC/FXC seçimi. DXC varsayılan; FXC eksik/hatalı dokular için #18 testinde doğrulanan uyumluluk alternatifi. Yeniden başlatma gerekir, önbellekler ayrıdır.
- Yeni işlevler bağımsız ve varsayılan kapalı; 13 dil. Çeviriler, gölgeler, kupalar, karakterler, kayıtlar ve önceki düzeltmeler korunur. Zorunlu anizotropi kapalı; film greni yeniden başlatma ister. ZIP tamamını bin/ ve assets/ ile çıkarın. Extractor v1.0.1 ayrı; oyun dosyaları ve kişisel ayarlar dahil değil.

## العربية

- واجهة قتال اختيارية للصحة والحالة والسلاح والذخيرة والقداحة، مع أثر الضرر ولمعان متحرك وسم وغاز وتحجيم حسب الدقة وتلاشي بعد خمس ثوان. حدود الحالة مطابقة للحقيبة الأصلية: 120/60/30 نقطة.
- إعادة تلقيم يدوية اختيارية دون الحقيبة: صوب ثم اضغط الزر المعين. تعمل لوحة المفاتيح ووحدة التحكم معاً، مع الحفاظ على قواعد الذخيرة والحركات الأصلية.
- أربعة اختصارات معدات قابلة للتعديل داخل اللعبة. اضغط L1 مطولاً وحدد بالعصا اليمنى؛ المثلث للتعديل وإكس للتأكيد. لوحة المفاتيح: V وI/J/K/L وF وEnter. قابلة للتغيير في تبويب HUD. الأسلحة المعينة تتبادل المواقع ويمكن النقل لخانة فارغة. معدات محمولة فقط. التحرير أثناء التعديل يلغي، وبعد التأكيد يجهز ويغلق.
- اختيار DXC/FXC في الأداء. يبقى DXC الافتراضي؛ FXC بديل توافق للخامات المفقودة أو الخاطئة، أكدته تجربة المشكلة #18. يتطلب إعادة تشغيل وله ذاكرة مؤقتة منفصلة.
- الميزات مستقلة ومعطلة افتراضياً، مع 13 لغة. الحفاظ على الترجمات والظلال والكؤوس والشخصيات والحفظ والإصلاحات. التصفية متباينة الخواص المفروضة معطلة، والتحبب يتطلب إعادة التشغيل. استخرج ZIP كاملاً مع bin/ وassets/. Extractor v1.0.1 منفصل دون بيانات اللعبة أو الإعدادات الشخصية.

## 简体中文

- 可选战斗HUD：生命、状态、武器、装填及备用弹药、打火机。含受伤残影、移动高光、中毒/毒气颜色、分辨率缩放和五秒无操作淡出。状态阈值与原物品栏一致：120/60/30 HP。
- 可选手动装填，无需打开物品栏：瞄准后按指定键。键盘和手柄可同时使用，保持原弹药及动画规则。
- 四个可在游戏中编辑的装备快捷位。按住L1，用右摇杆选择，三角编辑，叉键确认。键盘默认V、I/J/K/L、F、Enter，可在HUD页重设。已分配武器交换位置，空位可移动，仅限随身装备。编辑中松开取消，确认后松开则装备并关闭。
- 性能页新增DXC/FXC选择。默认DXC；FXC是纹理缺失或错误时的兼容选项，已在问题#18测试确认。需重启，各用独立缓存。
- 各功能独立且默认关闭，支持13种语言。保留翻译、阴影、奖杯、角色、存档及修复。强制各向异性过滤保持关闭，胶片颗粒仍需重启。完整解压ZIP并保留bin/和assets/。Extractor v1.0.1单独下载，不含游戏数据或个人设置。

## 한국어

- 선택형 전투 HUD: 체력, 상태, 무기, 장전/예비 탄약, 라이터. 피해 잔상, 움직이는 광택, 중독/가스, 해상도 비례 크기, 5초 후 사라짐. 상태 기준은 원본 인벤토리와 같은 120/60/30 HP입니다.
- 인벤토리 없이 선택형 수동 재장전: 조준하고 지정 버튼을 누르세요. 키보드와 컨트롤러 동시 사용, 원본 탄약과 애니메이션 규칙 유지.
- 게임에서 편집할 수 있는 네 장비 단축 슬롯. L1을 누른 채 오른쪽 스틱으로 선택, 세모로 편집, 엑스로 확인. 키보드 기본 V, I/J/K/L, F, Enter. HUD에서 변경 가능. 지정 무기는 자리를 교환하고 빈 슬롯으로 이동 가능. 소지품만 사용. 편집 중 놓으면 취소, 확인 후 놓으면 장착하고 닫힙니다.
- 성능에서 DXC/FXC 선택. DXC가 기본이며 FXC는 누락되거나 잘못된 텍스처의 호환 대안으로 문제 #18에서 확인되었습니다. 재시작 필요, 캐시 분리.
- 각 기능 독립 및 기본 꺼짐, 13개 언어. 번역, 그림자, 트로피, 캐릭터, 저장 및 수정 유지. 강제 이방성 필터 꺼짐, 필름 그레인은 재시작 필요. 전체 ZIP과 bin/, assets/ 유지. Extractor v1.0.1 별도, 게임 데이터나 개인 설정 미포함.

## Русский

- Необязательный боевой HUD: здоровье, состояние, оружие, патроны и зажигалка; след урона, движущийся блик, яд/газ, масштабирование и исчезновение через пять секунд. Пороги состояния как в оригинальном инвентаре: 120/60/30 HP.
- Необязательная ручная перезарядка без инвентаря: прицельтесь и нажмите назначенную кнопку. Клавиатура и контроллер вместе; правила патронов и анимаций сохранены.
- Четыре быстрых слота с редактированием в игре. Удерживайте L1 и выбирайте правым стиком; Треугольник редактирует, Крест подтверждает. Клавиатура: V, I/J/K/L, F, Enter. Переназначение во вкладке HUD. Назначенное оружие меняется местами, в пустой слот перемещается. Только вещи при себе. Отпустить при редактировании — отмена; после подтверждения — экипировать и закрыть.
- Выбор DXC/FXC в производительности. DXC по умолчанию; FXC — вариант совместимости при отсутствующих/неверных текстурах, подтверждённый в проблеме #18. Нужен перезапуск, кэши раздельные.
- Функции независимы и изначально выключены; 13 языков. Переводы, тени, трофеи, персонажи, сохранения и исправления сохранены. Принудительная анизотропия выключена; зерно требует перезапуска. Распакуйте весь ZIP с bin/ и assets/. Extractor v1.0.1 отдельно; без данных игры и личных настроек.
