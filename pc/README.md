# Kinect 3D Studio — versão para PC

Programa para Windows e Linux que **espera o Kinect ser conectado** e então pergunta:
**Ver sensores** ou **Jogar**. Instruções para quem vai usar: [packaging/LEIA-ME.txt](packaging/LEIA-ME.txt).

## O que tem

- **Espera pelo Kinect**: verifica o USB a cada segundo. Reconhece Kinect v1 (1414, 1473, Kinect for Windows),
  avisa quando falta a fonte de 12 V e recusa Kinect v2/Azure com o motivo. Conecta sozinho quando o sensor aparece
  e volta para a espera se o cabo sair.
- **Ver sensores**: profundidade (Turbo/cinza, faixa, filtros), câmera RGB, infravermelho, RGB alinhado,
  corpo rastreado, medição de distância por clique, captura de quadros, gravação `.k3drec` e motor (1414).
  Usa o mesmo núcleo de sensores do app Android (`app/src/main/cpp/sensors`).
- **Jogar — Expedição Ativa** (jogos originais):
  - 5 minijogos: Corredeira, Rebote, Esquiva, Aquário e Gravidade Zero, cada um em 3 dificuldades;
  - Modo Aventura: 5 expedições × 3 aventuras, medalhas (bronze/prata/ouro/platina) e desbloqueio;
  - Jogo livre com recordes; 20 emblemas; perfil salvo;
  - Fotos automáticas nos melhores momentos (com a foto da câmera RGB quando há Kinect);
  - 1 ou 2 jogadores ao mesmo tempo;
  - Menus por gesto (segurar a mão sobre o botão), pausa com as duas mãos para o alto;
  - Orientações de posição ("afaste-se", "vá para a esquerda"...).
- **Simulação sem Kinect**: boneco controlado por teclado e mouse, sempre identificado como SIMULAÇÃO.

## Como o corpo é rastreado

O libfreenect não tem esqueleto (isso era do SDK da Microsoft, só no Windows e com licença própria).
O rastreador em `src/body/` trabalha direto com a profundidade medida:

1. estima o plano do chão (RANSAC + mínimos quadrados) e remove o chão;
2. separa pessoas por componentes conexos com continuidade de profundidade (até 2 jogadores);
3. mede altura em pé, pulo (pés saindo do chão), agachamento, inclinação (quadril → cabeça),
   mãos (extremos da silhueta), batida de braços e a silhueta numa grade de 4 cm no "plano de jogo".

### Linux: OpenNI2 + NiTE2 (opcional)

No Linux o programa também rastreia com **OpenNI2 + NiTE2**:

- `libOpenNI2.so` é compilado a partir de `third_party/OpenNI2` (núcleo do OpenNI2 da structureio, Apache-2.0)
  e fica ao lado do executável; `OpenNI2/Drivers/libFreenectDriver.so` é o driver do libfreenect
  (`third_party/FreenectDriver`) que faz o OpenNI2 enxergar o Kinect v1.
- O **NiTE2 é proprietário e foi descontinuado**: não está no repositório nem no pacote. Ele é carregado em
  tempo de execução (`dlopen`) de `nite/` ao lado do programa, `$NITE2_PATH`, `$NITE2_REDIST64`,
  `~/.local/share/kinect3dstudio/nite` ou `~/NiTE-Linux-*/Redist`.
- Com o NiTE, o rastreador usa o mapa de usuários do NiTE (separação de pessoas), o chão estimado por ele e as
  articulações (mãos, centro da cabeça, quadril → inclinação). Altura, pulo, agachamento e a silhueta continuam
  vindo da profundidade. Sem o NiTE, ou se ele falhar, volta sozinho para o rastreador próprio.
- A estrutura dos quadros do NiTE é conferida a cada quadro (tamanhos, ids, coordenadas finitas); se não bater,
  o modo NiTE é desligado com aviso. Imagem espelhada é detectada pela posição da cabeça no mapa de usuários.
- `src/kinect/nite_capi.h` declara a API C do NiTE 2.2; `src/kinect/nite_tracker.cpp` carrega e lê;
  `BodyTracker::update(..., ExternalBodies*)` recebe rótulos e esqueletos.
- Diagnóstico: `KinectStudio --abrir-kinect` abre o sensor na partida sem esperar o vigia do USB.

Os jogos usam a silhueta real para colisões: qualquer parte do corpo rebate bolas, tapa vazamentos ou
estoura bolhas. As mãos são estimativas da silhueta, não articulações medidas.

## Compilar

Linux (Ubuntu/Debian):
```
sudo apt install build-essential cmake git xorg-dev libgl1-mesa-dev
bash pc/scripts/build_linux.sh          # compila, roda os testes, gera dist/KinectStudio-Linux.tar.gz
```
OpenNI2 + driver Freenect entram por padrão no Linux (`-DK3D_WITH_NITE=OFF` desliga). Não precisam de
pacotes extras (o USB do XnLib, que pediria libudev, é só dos drivers PrimeSense e fica de fora).

Windows (compilação cruzada a partir do Linux, baixa o llvm-mingw sozinho):
```
bash pc/scripts/build_windows.sh        # gera dist/KinectStudio-Windows.zip
```

No GitHub, o workflow `.github/workflows/build-pc.yml` faz os dois e publica os pacotes em **Artifacts**,
junto com capturas de tela do programa rodando em simulação.

## Testes

| Teste | O que verifica |
|---|---|
| `tracker_test` | chão em 3 inclinações, posição, altura, pulo, agachar, inclinar, mãos, batida de braços, 2 jogadores, avisos |
| `games_test` | os 15 níveis jogados do início ao fim por um robô através do rastreador real (com ruído); jogar rende medalha e ficar parado não; 2 jogadores; determinismo |
| `adventure_test` | medalhas, desbloqueio, emblemas, salvar/carregar perfil (inclusive arquivo corrompido) |
| `kinect_test` | identificação USB por VID/PID, vigia sem Kinect, abertura sem dispositivo, conversão bruto→mm |
| `nite_test` (Linux) | OpenNI2 carrega o driver Freenect; procura do NiTE e mensagens; caminho completo com um **NiTE falso** (`tests/fake_nite`, gera quadros com o simulador): pasta de dados, leitura em thread, coordenadas, mão levantada do lado certo, pulo, chão, espelhamento, quadros inválidos desligam o modo NiTE |

| `android_mode_test` | o jogo compilado em modo Android (`__ANDROID__`) rodando no PC com um "Kotlin" falso: espera, fonte de quadros entregue, menu, revogação (cabo arrancado) e destruição da fonte sem o jogo tocar nela depois |

`KinectStudio --shots PASTA` percorre todas as telas em simulação e salva capturas (usado na verificação).
`--jogar` abre direto um jogo em simulação; `--toque` mostra os botões de toque do Android também no PC.

O mesmo código (`src/body`, `src/game`, `src/app`) é compilado no app Android (veja o README da raiz):
lá o Kinect é aberto pelo Kotlin e entregue ao jogo por `app/src/main/cpp/game/game_bridge.cpp`.

## Licenças

Código do projeto: Apache-2.0. libfreenect e OpenNI2-FreenectDriver: Apache-2.0. OpenNI2: Apache-2.0.
NiTE2: proprietário, não incluído. libusb: LGPL-2.1 (DLL/SO separada).
raylib: zlib. Fonte Poppins: SIL OFL 1.1. Expedição Ativa é um jogo original: não usa nomes,
personagens, músicas, imagens ou fases de jogos da Microsoft.
