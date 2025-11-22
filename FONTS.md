# Tauwerk Font System

## Übersicht
Multi-Font-System mit Lazy-Loading und GPU-beschleunigtem Rendering.

## Verfügbare Fonts

### FontType::DEFAULT
- **Datei**: `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`
- **Verwendung**: Allgemeiner UI-Text, Labels, Buttons
- **Eigenschaften**: Gut lesbar, vollständiger Unicode-Support

### FontType::DIGITAL  
- **Datei**: `/home/tauwerk/assets/fonts/ds_digital/ds_digi_bold.ttf`
- **Verwendung**: Numerische Werte, BPM, Hz, Prozente
- **Eigenschaften**: Retro-Display-Look, perfekt für Sequencer-Ästhetik

### FontType::ICONS
- **Datei**: `/home/tauwerk/assets/fonts/tauwerk/tauwerk.ttf`
- **Verwendung**: Custom Icons, kann mit Text kombiniert werden
- **Eigenschaften**: Eigene Icon-Font für UI-Symbole

## Verfügbare Größen
Das System unterstützt beliebige Pixelgrößen, empfohlen sind:
- **16px**: Kleine Labels, Info-Text
- **24px**: Standard UI-Text (Default)
- **32px**: Überschriften, hervorgehobener Text
- **48px**: Große Werte, wichtige Anzeigen

## Verwendung

### Basis
```cpp
auto* label = ui.add_widget<Label>(
    x, y, 
    "Text",
    Color(1, 1, 1, 1),          // Weiß
    FontType::DEFAULT,          // Font-Typ
    24                          // Größe in px
);
```

### Beispiele

**Standard-Text:**
```cpp
auto* title = ui.add_widget<Label>(50, 50, "Sequencer", 
    Color(1, 1, 1, 1), FontType::DEFAULT, 32);
```

**Digital-Display (BPM-Anzeige):**
```cpp
auto* bpm = ui.add_widget<Label>(100, 100, "120 BPM",
    Color(0, 1, 0, 1),         // Grün = Retro-Display
    FontType::DIGITAL, 48);
```

**Kleine Info:**
```cpp
auto* info = ui.add_widget<Label>(10, 460, "v1.0",
    Color(0.5f, 0.5f, 0.5f, 1), FontType::DEFAULT, 16);
```

**Icons (wenn verfügbar):**
```cpp
auto* icon = ui.add_widget<Label>(50, 50, "A",  // Icon-Zeichen
    Color(1, 0.8f, 0, 1), FontType::ICONS, 32);
```

### Font dynamisch ändern
```cpp
label->set_font(FontType::DIGITAL, 32);
```

### Widget-Labels (Fader & Button)
Widgets haben integrierte Label-Unterstützung (5px über Widget, 16px):
```cpp
auto* fader = ui.add_widget<Fader>(50, 150, 700, 60);
fader->set_name("Master Volume");  // Label über Fader
// Value wird automatisch im Fader angezeigt (Digital-Font 24px)

auto* button = ui.add_widget<Button>(50, 250, 150, 60, "PLAY");
button->set_name("Transport");  // Label über Button
```

**Fader-Value-Display:**
- Automatisch im Fader positioniert (10px padding-left)
- Digital-Font 24px
- Dynamische Textfarbe:
  - **Schwarz** wenn auf weißem Fill
  - **Weiß** wenn auf Dither-Hintergrund
- Kein Performance-Impact (einfacher Position-Check)

## Performance

### Lazy-Loading
Fonts werden **nur bei Bedarf** geladen:
```
✅ Font loaded: DEFAULT (24px)      # Beim ersten Label mit 24px
✅ Font loaded: DIGITAL (48px)      # Beim ersten Digital-Label mit 48px
```

### Glyph-Caching
Pro Font+Größe werden **96 Glyphs** (ASCII 32-127) vorgerendert:
- 1 Font á 1 Größe = ~96 OpenGL-Texturen
- 3 Fonts á 4 Größen = ~1152 Texturen (ca. 2-4MB VRAM)

### Memory-Footprint
- **Minimal**: 1 Font, 1 Größe = ~500KB
- **Typisch**: 2-3 Fonts, 2-3 Größen = ~1-2MB
- **Maximum**: Alle 3 Fonts, alle 4 Größen = ~4MB

## Technische Details

### Font-Cache-Struktur
```cpp
std::map<FontCacheKey, std::map<char, Glyph>> font_cache;
```

**FontCacheKey:**
```cpp
struct FontCacheKey {
    FontType type;   // DEFAULT, DIGITAL, ICONS
    int size;        // Pixel-Größe
};
```

### Rendering-Pipeline
1. `Label::draw()` → ruft `Renderer::draw_text()` mit Font-Parametern
2. `Renderer::get_font_glyphs()` → prüft Cache
3. Falls nicht gecacht: `load_font()` → FreeType2 → OpenGL-Texturen
4. GPU-Rendering mit Text-Shader

### Typography-Korrektheit
Das System verwendet **Top-Left-Positionierung** statt Baseline:
- Font-Metrics (Ascender/Descender) werden beim Laden extrahiert
- Y-Position = Top der Bounding-Box (nicht Baseline)
- **Kein Überlauf** von Descenders (p, g, q, y) oder Ascenders (h, k, l)
- Ideal für UI-Layouts mit festen Widget-Höhen

```cpp
// Intern: baseline_y = y + metrics->ascender
// Text bleibt innerhalb der visuellen Bounding-Box
```

### Cleanup
Beim Shutdown werden automatisch alle Font-Texturen freigegeben:
```cpp
void Renderer::cleanup() {
    for (auto& cache_entry : font_cache) {
        for (auto& glyph_pair : cache_entry.second) {
            glDeleteTextures(1, &glyph_pair.second.texture_id);
        }
    }
}
```

## Best Practices

1. **Font-Größen begrenzen**: Nutze die 4 empfohlenen Größen (16/24/32/48)
2. **Digital-Font sparsam**: Nur für numerische Werte (BPM, Hz, %)
3. **Farben abstimmen**: 
   - Digital = Grün/Cyan (Retro-Display-Look)
   - Default = Weiß/Grau (lesbar)
4. **Performance**: Font-Änderungen zur Laufzeit sind teuer → bei Init festlegen

## Neue Fonts hinzufügen

### 1. Font-Datei kopieren
```bash
cp myfont.ttf /home/tauwerk/assets/fonts/
```

### 2. FontType erweitern
```cpp
// src/core/Types.h
enum class FontType {
    DEFAULT,
    DIGITAL,
    ICONS,
    MYFONT    // <-- Neu
};
```

### 3. Pfad registrieren
```cpp
// src/core/Renderer.cpp
const char* Renderer::get_font_path(FontType type) {
    switch (type) {
        // ...
        case FontType::MYFONT:
            return "/home/tauwerk/assets/fonts/myfont.ttf";
    }
}
```

### 4. Verwenden
```cpp
auto* label = ui.add_widget<Label>(x, y, "Text",
    Color(1, 1, 1, 1), FontType::MYFONT, 24);
```
