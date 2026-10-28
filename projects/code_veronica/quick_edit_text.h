#pragma once
#include <algorithm>
namespace quick_edit_text {
enum { Edit,Pick,Confirm,Cancel,SwapHelp,ChooseHelp,Empty };
inline const wchar_t* get(int language,int field){
 static const wchar_t* rows[13][7]={
 {L"Editar",L"Substituir atalho",L"Confirmar",L"Soltar: cancelar",L"Armas em outro atalho trocam de lugar. Um espaço vazio permite mover.",L"Selecione um espaço; use Editar para escolher entre os equipamentos carregados.",L"Espaço vazio"},
 {L"Edit",L"Replace shortcut",L"Confirm",L"Release: cancel",L"Weapons already in a shortcut swap places. An empty slot allows moving.",L"Select a slot; use Edit to choose from your carried equipment.",L"Empty slot"},
 {L"Editar",L"Sustituir acceso",L"Confirmar",L"Soltar: cancelar",L"Las armas de otro acceso intercambian posiciones. Un espacio vacío permite mover.",L"Selecciona un espacio; usa Editar para elegir equipo que llevas.",L"Espacio vacío"},
 {L"編集",L"ショートカット変更",L"決定",L"離す：キャンセル",L"登録済みの装備は位置を入れ替えます。空き枠には移動します。",L"枠を選択して編集を押すと、所持している装備から選べます。",L"空きスロット"},
 {L"Modifier",L"Remplacer le raccourci",L"Confirmer",L"Relâcher : annuler",L"Les armes déjà assignées échangent leur place. Un emplacement vide permet de déplacer.",L"Sélectionnez un emplacement puis Modifier pour choisir un équipement porté.",L"Emplacement vide"},
 {L"Bearbeiten",L"Schnellplatz ersetzen",L"Bestätigen",L"Loslassen: abbrechen",L"Bereits zugewiesene Waffen tauschen Plätze. Ein leerer Platz erlaubt Verschieben.",L"Platz auswählen und Bearbeiten drücken, um mitgeführte Ausrüstung zu wählen.",L"Leerer Platz"},
 {L"Modifica",L"Sostituisci scorciatoia",L"Conferma",L"Rilascia: annulla",L"Le armi già assegnate scambiano posizione. Uno slot vuoto permette di spostare.",L"Seleziona uno slot e usa Modifica per scegliere tra gli oggetti trasportati.",L"Slot vuoto"},
 {L"Edytuj",L"Zmień skrót",L"Potwierdź",L"Zwolnij: anuluj",L"Przypisane bronie zamieniają się miejscami. Puste miejsce pozwala przenieść.",L"Wybierz miejsce i Edytuj, aby wybrać noszone wyposażenie.",L"Puste miejsce"},
 {L"Düzenle",L"Kısayolu değiştir",L"Onayla",L"Bırak: iptal",L"Atanmış silahlar yer değiştirir. Boş bir yuvaya taşınabilir.",L"Yuva seçip Düzenle ile taşınan ekipmanlardan birini seçin.",L"Boş yuva"},
 {L"تعديل",L"تغيير الاختصار",L"تأكيد",L"حرر للإلغاء",L"تتبادل الأسلحة المعينة مواقعها. تسمح الخانة الفارغة بالنقل.",L"حدد خانة ثم اضغط تعديل لاختيار معدات تحملها.",L"خانة فارغة"},
 {L"编辑",L"替换快捷装备",L"确认",L"松开：取消",L"已分配的武器会交换位置。可移至空槽位。",L"选择槽位并按编辑，从随身装备中选择。",L"空槽位"},
 {L"편집",L"단축 장비 변경",L"확인",L"놓기: 취소",L"지정된 무기는 위치를 교환합니다. 빈 슬롯으로 이동할 수 있습니다.",L"슬롯을 고르고 편집을 눌러 소지한 장비를 선택하세요.",L"빈 슬롯"},
 {L"Изменить",L"Заменить быстрый слот",L"Подтвердить",L"Отпустить: отмена",L"Назначенное оружие меняется местами. В пустую ячейку можно переместить.",L"Выберите ячейку и нажмите Изменить, чтобы выбрать снаряжение при себе.",L"Пустая ячейка"}
 };return rows[std::clamp(language,0,12)][std::clamp(field,0,6)];
}
}
