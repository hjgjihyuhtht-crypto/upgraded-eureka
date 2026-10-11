# Formatos de arquivo

Todos os arquivos ficam na área privada do app (`files/`). A tela **Capturas e gravações** exporta para uma pasta escolhida.

## Captura de quadro — pasta `CAP_aaaaMMdd_HHmmss_SSS/`

| Arquivo | Conteúdo |
|---|---|
| `depth_raw.pgm` | Profundidade **original do sensor**: 640×480, PGM binário 16 bits (big-endian), valores de 11 bits (0–2046). `2047` = sem medida. |
| `depth_mm.pgm` | Mesma imagem convertida em milímetros pela tabela de calibração de fábrica do Kinect. `0` = sem medida. Sem filtros. |
| `depth_mm_filtrado.pgm` | Só existe se um filtro estava ativo. Os filtros nunca preenchem buracos. |
| `rgb.ppm` / `ir.pgm` | Quadro de vídeo original (RGB 640×480 24 bits, ou IR 640×488 8 bits), sem ajustes de brilho/contraste. |
| `rgb_alinhado.ppm` | RGB reamostrado na grade da profundidade (registro pela calibração de fábrica). Preto onde não há medida. |
| `calibration.bin` | Calibração completa do sensor (ver abaixo). |
| `depth_visualizacao.png`, `rgb.png`, `ir.png` | Prévias para visualizar em qualquer aplicativo. |
| `metadata.json` | Data, modelo, modo, timestamps do sensor e do celular, diferença de tempo entre os fluxos, intrínsecos e parâmetros de visualização. |

PGM/PPM abrem no GIMP, ImageMagick, OpenCV (`cv2.imread(..., cv2.IMREAD_UNCHANGED)`) e numpy.

### Converter profundidade em 3D

Com `fx`, `fy`, `cx`, `cy` de `metadata.json → intrinsics` e `Z` em mm de `depth_mm.pgm`:

```
X = (u − cx) · Z / fx
Y = (v − cy) · Z / fy
```

Eixos da câmera IR: X para a direita, Y para baixo, Z para a frente. Os intrínsecos vêm da
calibração gravada no próprio Kinect (`fx = reference_distance / (2 · reference_pixel_size)`, centro 320, 240),
a mesma relação usada por `freenect_camera_to_world` no libfreenect.

## Gravação — `REC_aaaaMMdd_HHmmss.k3drec`

Binário little-endian, escrito em fluxo (sem índice no fim), para que uma gravação interrompida
(bateria, app fechado) continue legível até o último quadro completo.

```
Cabeçalho
  8 bytes   "K3DREC" 0x00 0x01
  u32       tamanho do JSON
  bytes     JSON (UTF-8): app, data, modelo, modo, intrínsecos
  u32       tamanho da calibração
  bytes     calibração (formato abaixo)

Registros (repetidos)
  u32       0x314D5246 ("FRM1")
  u8        tipo: 1 = profundidade bruta (uint16/pixel), 2 = RGB888, 3 = IR 8 bits
  u8[3]     zero
  u16       largura
  u16       altura
  u32       timestamp do sensor (contador interno do Kinect)
  i64       instante no celular (ns, relógio monotônico)
  u32       tamanho dos dados
  bytes     dados
```

Taxa aproximada a 30 Hz: profundidade + RGB ≈ 46 MB/s; profundidade + IR ≈ 28 MB/s; só profundidade ≈ 18 MB/s.
A fila de escrita usa no máximo 48 MB de RAM; se o armazenamento não acompanhar, quadros são descartados e
contados (o app mostra quantos).

## Calibração — `calibration.bin` (também embutida no `.k3drec`)

```
u32  0x4B334341 ("K3CA")     u32  versão (1)
f32  dcmos_emitter_dist (cm) f32  dcmos_rcmos_dist (cm)
f32  reference_distance (mm) f32  reference_pixel_size (mm)
u32  target_offset
u32 n + u16[n]  tabela bruto→mm (n = 2048)
u32 n + i32[n]  deslocamento horizontal profundidade→RGB por mm (n = 10000)
u32 n + i32[n]  tabela de registro 640×480×(x·256, y)
```

Para o pixel de profundidade (x, y) com distância `mm`, o pixel RGB correspondente é
`rx = (tabela[2i] + desloc[mm]) / 256`, `ry = tabela[2i+1]` (i = y·640 + x), menos `target_offset` no índice linear.
