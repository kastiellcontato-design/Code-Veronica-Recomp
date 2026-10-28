#pragma once
#include <string>
#include <algorithm>
#include "cv_enhanced_i18n.h"
#include "cv_enhanced_settings.h"
extern "C" int cv_modern_get();
extern "C" void cv_modern_change(int);
inline std::string modern_text(int id){
 static const wchar_t* const words[13][4]={
 {L"Estilo de movimento",L"Clássico",L"Moderno",L"Moderno: mova na direção da tela. Mantém a direção nos cortes de câmera."},
 {L"Movement style",L"Classic",L"Modern",L"Modern: move relative to the screen. Holds direction through camera cuts."},
 {L"Estilo de movimiento",L"Clásico",L"Moderno",L"Moderno: muévete según la pantalla. Mantiene la dirección al cambiar de cámara."},
 {L"移動操作",L"クラシック",L"モダン",L"画面の方向に移動。カメラが切り替わっても進行方向を維持します。"},
 {L"Style de déplacement",L"Classique",L"Moderne",L"Moderne : déplacement relatif à l’écran, direction conservée aux changements de caméra."},
 {L"Bewegungssteuerung",L"Klassisch",L"Modern",L"Modern: Bewegung relativ zum Bild. Die Richtung bleibt bei Kamerawechseln erhalten."},
 {L"Stile di movimento",L"Classico",L"Moderno",L"Moderno: movimento relativo allo schermo, direzione mantenuta ai cambi di camera."},
 {L"Styl poruszania",L"Klasyczny",L"Nowoczesny",L"Ruch względem ekranu. Zachowuje kierunek przy zmianie kamery."},
 {L"Hareket tarzı",L"Klasik",L"Modern",L"Ekrana göre hareket eder. Kamera değişince hareket yönü korunur."},
 {L"أسلوب الحركة",L"كلاسيكي",L"حديث",L"الحركة حسب اتجاه الشاشة مع الحفاظ على الاتجاه عند تبديل الكاميرا."},
 {L"移动方式",L"经典",L"现代",L"按画面方向移动，镜头切换时保持移动方向。"},
 {L"이동 방식",L"클래식",L"모던",L"화면 방향으로 이동하며 카메라 전환 시 이동 방향을 유지합니다."},
 {L"Управление движением",L"Классическое",L"Современное",L"Движение относительно экрана с сохранением направления при смене камеры."}
 };
 const auto* w=words[std::clamp(cv_enhanced_language(),0,12)][id];
 int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);
 std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;
}
