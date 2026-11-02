# Optical Design and 3D Printing Guidelines

## 1. Optical Objective: Soft Diffusion vs. Clear Crystal

WS2812B 5mm SMD LEDs are point light sources. Without proper diffusion, the bare emitter creates an uncomfortably bright hotspot surrounded by dark shadows.

For a warm, cozy holiday aesthetic, csiGarland uses **soft volumetric diffusion** with translucent PETG:
* The figurines, in the 50 mm height class (45.7–49.0 mm), are printed completely **hollow (0% infill)**.
* Wall thickness is **1.40–1.50 mm** at least: 3 perimeter loops with a 0.4 mm nozzle.
* Light bounces internally and exits evenly across the entire surface of the figurine.
* Finished weight per cap is about **6.7 grams** (214 g for 32 caps, Bambu Studio's estimate).

---

## 2. Bambu Studio Slicing Profile

The ready project, and the full list of its settings, is in [`3d-model/README.md`](../../3d-model/README.md). This table follows it.

| Setting | Recommended Value | Optical Reason |
|---|---|---|
| **Material** | Bambu Translucent PETG | Excellent light transmission with soft tint |
| **Nozzle Temperature** | 245 °C | The `Bambu PETG Translucent` preset the print project uses |
| **Wall Loops** | 3 | At least 1.40 mm: sturdy, and still translucent |
| **Sparse Infill** | 0% | Completely hollow interior for maximum internal light bounce |
| **Top Shell Layers** | 6 | Clean closure on pointed ornaments (stars, trees, icicles) |
| **Bottom Shell Layers** | 3–4 | Solid base for snap-fit attachment |
| **Layer Height** | 0.20 mm, 0.16 mm for pinecone, snowman and gingerbread | As in the print project |
| **Outer Wall Speed** | 40–60 mm/s | Slower speed increases optical uniformity of PETG |
| **Seam Position** | Back or Aligned on rib | Hides the vertical seam from the primary viewing angle |

---

## 3. Optical Enhancements in CAD

### 3.1 Multi-Stage Internal Diffusion Chambers
To prevent the central beam of the WS2812B from burning a bright spot on the collar or tip while keeping all overhangs self-supporting:
* Each cap uses a staged internal light cavity tailored to its outer silhouette: a lower clearance and entry cone above the LED socket ($r_1 = 4.2\text{–}5.5\text{ mm}$, or an $\varnothing 8.8\text{ mm}$ pocket with a $45^\circ$ conical transition in the narrow-neck star and gingerbread caps) leads into a central diffusion chamber that follows the figurine's body while maintaining $\ge 1.40\text{–}1.50\text{ mm}$ walls everywhere.
* Sloped internal ceilings ($\le 45^\circ$ from vertical) and faceted or domed upper chambers scatter the upward LED beam across the side perimeters without requiring internal print supports.

### 3.2 "Frosted Glass" via Bambu Studio Fuzzy Skin
To eliminate plastic glossiness and achieve the look of frosted ice or sugar glass:
* Enable **Fuzzy Skin** on *Contour only* (outer perimeter):
  * `Fuzzy skin point distance:` **0.10 – 0.15 mm**
  * `Fuzzy skin thickness:` **0.05 – 0.08 mm**
* This micro-texture creates a matte frosted surface that diffuses light outward in all directions.

### 3.3 Faceted / Low-Poly Geometries
* Low-poly models (crystals, faceted stars, geometric trees) look exceptional with translucent PETG. Each facet captures internal light at different angles, creating a sparkling jewel effect without extra post-processing.

---

## 4. Mechanical Separation: Modular Base vs. Diffuser Cap

Rather than printing a monolithic figurine with a bottom opening, csiGarland utilizes a **two-piece modular architecture**:

```
 [ Diffuser Cap (~48mm) ] <-- Translucent PETG, hollow (~7g), snap-fit
           │
      (Snap / Twist)
           │
 [ Base Mount (17.6mm) ]  <-- Opaque PETG (Green/White), holds the LED board and hot-glued wires
           │
 ───[ 3-Wire Cable ]─── (Soldered and strain-relieved before assembly)
```

### Benefits of the Modular System:
1. **Simplified Assembly and Soldering:** The 32 LEDs are soldered into a continuous cable harness on the bench and snapped into the rugged base mounts. All electrical continuity and LED testing is completed before touching the translucent caps.
2. **Wire Strain Relief:** The wires run through a side channel of the base mount and are potted in it with hot glue, so pulling on the garland never reaches the solder pads.
3. **Interchangeable Themes:** The electronics harness can remain intact while diffuser caps are swapped (Christmas trees & snowflakes in December, hearts in February, or stars year-round).
