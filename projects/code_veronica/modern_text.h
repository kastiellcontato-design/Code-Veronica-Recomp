#pragma once
#include <string>
#include <algorithm>
#include "cv_enhanced_i18n.h"
#include "cv_enhanced_settings.h"
extern "C" int cv_modern_get();
extern "C" void cv_modern_change(int);
inline std::string modern_text(int id){
 static const wchar_t* const words[13][5]={
 {L"Estilo de movimento",L"Clássico",L"Moderno",L"Moderno: mova na direção da tela. Mantém a direção nos cortes de câmera.",L"Jogabilidade parecida com a de Resident Evil HD Remaster e Resident Evil 0 HD Remaster."},
 {L"Movement style",L"Classic",L"Modern",L"Modern: move relative to the screen. Holds direction through camera cuts.",L"Gameplay similar to Resident Evil HD Remaster and Resident Evil 0 HD Remaster."},
 {L"Estilo de movimiento",L"Clásico",L"Moderno",L"Moderno: muévete según la pantalla. Mantiene la dirección al cambiar de cámara.",L"Jugabilidad similar a la de Resident Evil HD Remaster y Resident Evil 0 HD Remaster."},
 {L"移動操作",L"クラシック",L"モダン",L"画面の方向に移動。カメラが切り替わっても進行方向を維持します。",L"バイオハザード HD リマスターとバイオハザード0 HD リマスターに近い操作感です。"},
 {L"Style de déplacement",L"Classique",L"Moderne",L"Moderne : déplacement relatif à l’écran, direction conservée aux changements de caméra.",L"Maniabilité proche de Resident Evil HD Remaster et Resident Evil 0 HD Remaster."},
 {L"Bewegungssteuerung",L"Klassisch",L"Modern",L"Modern: Bewegung relativ zum Bild. Die Richtung bleibt bei Kamerawechseln erhalten.",L"Spielgefühl ähnlich wie bei Resident Evil HD Remaster und Resident Evil 0 HD Remaster."},
 {L"Stile di movimento",L"Classico",L"Moderno",L"Moderno: movimento relativo allo schermo, direzione mantenuta ai cambi di camera.",L"Giocabilità simile a Resident Evil HD Remaster e Resident Evil 0 HD Remaster."},
 {L"Styl poruszania",L"Klasyczny",L"Nowoczesny",L"Ruch względem ekranu. Zachowuje kierunek przy zmianie kamery.",L"Rozgrywka podobna do Resident Evil HD Remaster i Resident Evil 0 HD Remaster."},
 {L"Hareket tarzı",L"Klasik",L"Modern",L"Ekrana göre hareket eder. Kamera değişince hareket yönü korunur.",L"Oynanış, Resident Evil HD Remaster ve Resident Evil 0 HD Remaster’a benzer."},
 {L"أسلوب الحركة",L"كلاسيكي",L"حديث",L"الحركة حسب اتجاه الشاشة مع الحفاظ على الاتجاه عند تبديل الكاميرا.",L"أسلوب اللعب مشابه لـ Resident Evil HD Remaster و Resident Evil 0 HD Remaster."},
 {L"移动方式",L"经典",L"现代",L"按画面方向移动，镜头切换时保持移动方向。",L"操作手感类似生化危机 HD 重制版和生化危机0 HD 重制版。"},
 {L"이동 방식",L"클래식",L"모던",L"화면 방향으로 이동하며 카메라 전환 시 이동 방향을 유지합니다.",L"바이오하자드 HD 리마스터, 바이오하자드 0 HD 리마스터와 비슷한 조작감입니다."},
 {L"Управление движением",L"Классическое",L"Современное",L"Движение относительно экрана с сохранением направления при смене камеры.",L"Управление похоже на Resident Evil HD Remaster и Resident Evil 0 HD Remaster."}
 };
 const auto* w=words[std::clamp(cv_enhanced_language(),0,12)][id];
 int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);
 std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;
}
