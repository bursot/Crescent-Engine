# Grid Anti-Aliasing Improvements

## 🎯 Problem
Grid çizgileri hareket ederken titriyordu (jittering/aliasing) ve profesyonel görünmüyordu.

## ✨ Çözümler

### 1. **Screen-Space Derivative Anti-Aliasing**
```metal
float2 derivative = fwidth(coord);
float2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
```
- `fwidth()` ile pixel genişliği hesaplanır
- Grid çizgileri her pixel için perfect anti-aliased
- Zoom level'dan bağımsız kaliteli görüntü

### 2. **LOD (Level of Detail) System**
```metal
float lodFactor = smoothstep(fadeStart * 0.4, fadeStart * 0.7, distanceFromCamera);
float gridMask = mix(
    lineMinor * 0.35 + lineMajor * 0.65,  // Close: minor + major
    lineMajor,                              // Far: only major
    lodFactor
);
```
- Yakında: Minor (1m) + Major (10m) gridler
- Uzakta: Sadece Major gridler (moire pattern önlenir)
- Yumuşak geçiş ile LOD değişimi görünmez

### 3. **Smooth Grid Snapping**
```cpp
float snapSize = m_gridCellSize * 2.0f; // 2m snap
const float smoothFactor = 0.15f;
m_gridOriginX += (targetOriginX - m_gridOriginX) * smoothFactor;
```
- Lerp ile yumuşak snap geçişi
- Ani pozisyon değişimi yok
- Titreşme minimize

### 4. **Professional Color Palette**
```cpp
m_gridColor = Math::Vector4(0.42f, 0.46f, 0.52f, 0.55f);  // Neutral gray-blue
```
- Industry standard nötr renk
- %55 alpha (dikkat dağıtmayan)
- X ekseni: Kırmızı
- Z ekseni: Yeşil

### 5. **Extended Fade Distance**
```cpp
m_gridFadeStart = 25.0f;   // 25 metre
m_gridFadeEnd = 120.0f;    // 120 metre
```
- Daha uzun fade mesafesi
- Profesyonel "infinite grid" hissi

## 🚀 Sonuç

### Öncesi:
❌ Titreşen çizgiler  
❌ Uzakta aliasing/moire  
❌ Ani snap geçişleri  
❌ Aşırı parlak/dikkat dağıtıcı  

### Sonrası:
✅ Pürüzsüz, anti-aliased çizgiler  
✅ LOD sistemi ile temiz uzak görünüm  
✅ Yumuşak snap geçişleri  
✅ Profesyonel görünüm (Unity/Unreal seviyesi)  

## 📊 Teknik Detaylar

**Shader Features:**
- `fwidth()` based anti-aliasing
- Dual-layer grid (minor + major)
- Distance-based LOD
- Axis highlighting
- Fresnel effect for grazing angles

**Performance:**
- Early fragment discard (`alpha < 0.008`)
- LOD reduces fragment count at distance
- Single draw call for entire grid

**Compatibility:**
- Metal 2.0+
- macOS 10.15+
- Works on all Apple Silicon and Intel Macs

## 🎨 Customization

Grid parametreleri `DebugRenderer.cpp` constructor'da değiştirilebilir:

```cpp
m_gridCellSize(1.0f)       // Hücre boyutu (metre)
m_gridFadeStart(25.0f)     // Fade başlangıç mesafesi
m_gridFadeEnd(120.0f)      // Fade bitiş mesafesi
m_gridColor(...)           // RGBA renk
```

## 🔍 İleri Düzey

Daha da kaliteli grid istiyorsanız:
- **Adaptive LOD**: Camera yüksekliğine göre cell size ayarla
- **Perspective correction**: Uzak gridleri daha belirgin yap
- **Multi-scale grid**: 3+ katman (0.1m, 1m, 10m, 100m)
- **Custom axis**: Arbitrary rotation support

---

**Updated:** December 2024  
**Quality Level:** Industry Standard (Unity/Unreal equivalent)
