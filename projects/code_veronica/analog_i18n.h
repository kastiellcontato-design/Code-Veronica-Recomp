#pragma once
/* Compact names leave room for three keyboard and two controller bindings. */
static const wchar_t* const cv_stick_names[13][2]={
 {L"Analógico E",L"Analógico D"},{L"Left stick",L"Right stick"},{L"Stick izq.",L"Stick der."},
 {L"左スティック",L"右スティック"},{L"Stick gauche",L"Stick droit"},{L"Linker Stick",L"Rechter Stick"},
 {L"Stick sinistro",L"Stick destro"},{L"Lewy drążek",L"Prawy drążek"},{L"Sol çubuk",L"Sağ çubuk"},
 {L"العصا اليسرى",L"العصا اليمنى"},{L"左摇杆",L"右摇杆"},{L"왼쪽 스틱",L"오른쪽 스틱"},{L"Левый стик",L"Правый стик"}};
static const wchar_t* const cv_axis_hints[13]={
 L"Analógicos: configure as direções abaixo. No modo Moderno, teclas do analógico esquerdo simulam inclinação total para correr; o D-pad mantém a caminhada.",
 L"Sticks: bind the directions below. In Modern mode, left-stick keys simulate full tilt to run; the D-pad keeps walking.",
 L"Configura las direcciones de los sticks. En modo Moderno, las teclas del stick izquierdo simulan inclinación máxima para correr; la cruceta permite caminar.",
 L"下でスティックの方向を割り当てます。モダンでは左スティックのキーで走り、十字キーで歩きます。",
 L"Configurez les directions des sticks ci-dessous. En mode Moderne, les touches du stick gauche font courir ; la croix directionnelle permet de marcher.",
 L"Stick-Richtungen unten belegen. Im modernen Modus simulieren die Tasten des linken Sticks vollen Ausschlag zum Rennen; das Steuerkreuz bleibt zum Gehen.",
 L"Assegna le direzioni degli stick. In modalità Moderna, i tasti dello stick sinistro simulano l'inclinazione massima per correre; la croce direzionale serve per camminare.",
 L"Przypisz kierunki drążków poniżej. W trybie nowoczesnym klawisze lewego drążka symulują pełne wychylenie, aby biec; krzyżak służy do chodzenia.",
 L"Çubuk yönlerini aşağıdan atayın. Modern modda sol çubuk tuşları koşmak için tam eğimi taklit eder; yön tuşları yürütür.",
 L"عيّن اتجاهات العصوين أدناه. في الوضع الحديث تحاكي مفاتيح العصا اليسرى الميل الكامل للركض، وتبقى أزرار الاتجاهات للمشي.",
 L"在下方设置摇杆方向。现代模式中，左摇杆按键模拟完全推动以奔跑；方向键用于行走。",
 L"아래에서 스틱 방향을 지정하세요. 모던 모드에서 왼쪽 스틱 키는 최대 기울기로 달리며, 방향 패드는 걷습니다.",
 L"Назначьте направления стиков ниже. В современном режиме клавиши левого стика имитируют полный наклон для бега; крестовина служит для ходьбы."};
static std::wstring cv_axis_name(int lang,int n){static const wchar_t* arrows[]={L"↑",L"↓",L"←",L"→"};if(lang<0||lang>=13)lang=1;return std::wstring(cv_stick_names[lang][n/4])+L" "+arrows[n%4];}
