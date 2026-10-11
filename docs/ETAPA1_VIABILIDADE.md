# Etapa 1 — Viabilidade técnica

## Hardware escolhido

**Microsoft Kinect v1 para Xbox 360** (modelos 1414 ou 1473), com a fonte de 12 V original.

## Comparação das gerações

| Geração | Barramento | Alimentação | Biblioteca aberta | Android | Decisão |
|---|---|---|---|---|---|
| Kinect v1 Xbox 360 (1414/1473) | USB 2.0 HS, isócrono | 12 V externa obrigatória | libfreenect (Apache-2.0 / GPL-2) | Viável com libusb + fd do `UsbManager` | **Suportado** |
| Kinect for Windows v1 | USB 2.0 HS, isócrono | 12 V externa | libfreenect | Viável (câmera); motor exige firmware proprietário | Suportado (sem motor) |
| Kinect v2 (Xbox One) | USB 3.0, isócrono | 12 V externa + adaptador | libfreenect2 | Exige USB 3.0 real no celular e porte do processamento de profundidade | Fora do escopo desta versão; detectado e recusado com motivo |
| Azure Kinect DK | USB 3.0 | USB-C PD ou fonte | SDK aberto, mas o *depth engine* é binário fechado (Windows/Linux glibc) | Sem profundidade no Android (bionic) | Não suportado; detectado e recusado com motivo |

## Kinect v1 no Android — como funciona

1. O Kinect v1 expõe um hub USB interno com três dispositivos (VID `0x045E`):
   câmera `0x02AE` (RGB, IR, profundidade), motor `0x02B0` (inclinação, LED, acelerômetro) e áudio `0x02AD`.
2. Um app Android sem root **não** pode listar `/dev/bus/usb`. Por isso:
   - O Kotlin pede permissão com `UsbManager.requestPermission()` e abre o dispositivo com `openDevice()`.
   - O file descriptor é passado ao C++, que chama `libusb_wrap_sys_device()` com `LIBUSB_OPTION_NO_DEVICE_DISCOVERY`.
   - O libfreenect original só abre dispositivos enumerando o barramento; por isso foi escrito
     `freenect_android_open.c`, que replica a lógica de `fnusb_open_subdevices()` a partir dos handles já abertos.
3. A câmera usa dois endpoints isócronos de entrada com 2 × 960 bytes por microframe cada,
   ≈ 30,7 MB/s no total, ou cerca de 64% do orçamento periódico de um barramento USB 2.0 high-speed.

## Requisitos físicos

- Celular com **USB Host (OTG)**. O app verifica `FEATURE_USB_HOST` e avisa se não houver.
- **Fonte de 12 V do Kinect ligada na tomada.** O celular não fornece essa tensão. Sem ela, a câmera não
  enumera; o app detecta essa situação (só motor/áudio presentes) e mostra o motivo.
- Adaptador do conector proprietário do Kinect para USB-A + fonte (o "adaptador de energia do Kinect").
- Adaptador OTG USB-A fêmea → USB-C/micro-USB macho. Evite hubs com outros dispositivos ativos.
- Nem todo controlador USB de celular entrega transferências isócronas de alta largura de banda com estabilidade.
  Não há como garantir isso antes de testar: o **teste de fluxos** do app mede na prática.

## Limitações conhecidas (e o que o projeto fará)

| Limitação | Causa | Plano |
|---|---|---|
| RGB e IR não simultâneos | Mesmo endpoint de vídeo no hardware v1 | Alternância de fluxo; profundidade acompanha qualquer um |
| Sem esqueleto pronto | O rastreamento do Kinect v1 vinha do SDK da Microsoft (somente Windows) | Estimativa de pose local (ex.: MediaPipe Pose, Apache-2.0) no RGB + elevação para 3D com a profundidade. Rotações serão estimadas |
| Motor do 1473/K4W | Controlado pelo chip de áudio, que exige o firmware proprietário `audios.bin` | Indisponível nesses modelos; câmera funciona |
| Resolução de profundidade | 640×480, faixa útil ~0,5–4 m, ruído crescente com a distância | Fusão TSDF de vários quadros na Etapa 5 |
| Sincronização | Profundidade e vídeo chegam em fluxos separados | Timestamps do sensor e do host registrados; defasagem informada |

## Bibliotecas e licenças

| Componente | Licença | Uso |
|---|---|---|
| libusb 1.0.27 | LGPL-2.1 | Biblioteca compartilhada separada (`libusb1.0.so`), substituível |
| libfreenect (OpenKinect) | Apache-2.0 (dupla com GPL-2; usada a opção Apache) | Ligada estaticamente |
| Jetpack Compose / AndroidX | Apache-2.0 | Interface |
