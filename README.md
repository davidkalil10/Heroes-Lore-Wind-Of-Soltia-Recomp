# ⚔️ Heroes Lore: Wind of Soltia — Native Recompilation (C++17 + SDL2)

[![Build & Release](https://github.com/davidkalil10/Heroes-Lore-Recomp/actions/workflows/build.yml/badge.svg)](https://github.com/davidkalil10/Heroes-Lore-Recomp/actions/workflows/build.yml)
[![GitHub Release](https://img.shields.io/github/v/release/davidkalil10/Heroes-Lore-Recomp?color=blue&label=Latest%20Release)](https://github.com/davidkalil10/Heroes-Lore-Recomp/releases)
[![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Game Lang](https://img.shields.io/badge/Game%20Lang-PT--BR%20%7C%20EN%20%7C%20IT%20%7C%20ES-brightgreen.svg)]()
[![Status](https://img.shields.io/badge/Status-100%25%20Playable%20%26%20Save%20Working-brightgreen.svg)]()

Port nativo, determinístico e de alta performance do lendário RPG J2ME **Heroes Lore: Wind of Soltia** (*Hands-On Mobile / EA Mobile*), baseado na versão oficial brasileira de resolução 240x320.

Diferente de remakes feitos "no olhômetro", este projeto executa o **bytecode original decompilado e reinterpretado diretamente em C++17** com uma camada de abstração fina para MIDP 2.0 / LCDUI / RMS sobre SDL2, preservando com precisão matemática absoluta cada fórmula de dano, atributos, comportamento de IA, diálogos, tabelas de eventos e taxas de drop.

---

## 📥 Downloads & Plataformas Suportadas

Os binários compilados oficiais e prontos para jogar de cada plataforma estão disponíveis na aba **[Releases](https://github.com/davidkalil10/Heroes-Lore-Recomp/releases)**:

| Plataforma | Formato | Tamanho | Como Jogar |
| :--- | :--- | :--- | :--- |
| 🎮 **Nintendo Switch** | `.nro` | ~7 MB | Copie `heroes_lore.nro` para `/switch/heroes_lore/` no cartão SD e abra via Homebrew Menu. |
| 🪟 **Windows x64** | `.zip` | ~12 MB | Baixe `heroes_lore_windows_x64.zip`, extraia e dê dois cliques em `heroes_lore.exe` (portátil, com DLLs e assets inclusos). |
| 🐧 **Linux / Steam Deck** | `.AppImage` | ~14 MB | Dê permissão com `chmod +x heroes_lore_linux_x86_64.AppImage` e execute com dois cliques ou terminal. |
| 📱 **Android** | `.apk` | ~2.5 MB | Instale `heroes_lore_android_arm64.apk` (smartphones modernos) ou `heroes_lore_android_universal.apk`. |

---

## 🌟 Destaques do Projeto

- **Localização Completa Multi-idioma em Tempo Real (PT-BR, EN, IT, ES):**
  - **4 idiomas suportados** com 3.951 strings alinhadas por ID: Português do Brasil (Open Mind Team 2008), Inglês oficial, Italiano oficial (BiNPDA) e Espanhol completo (diálogos, missões, itens e menus adaptados ao charset J2ME).
  - **Alternância Dinâmica:** Mude o idioma a qualquer momento pelo menu de **Opções** (na tela de título ou no menu de pause in-game) ou instantaneamente pelo atalho de teclado <kbd>F2</kbd>.
  - **Hot-Reload Imediato em C++:** O buffer de textos, itens/equipamentos do inventário, telas de status, submenus e os diálogos do mapa ativo (mesmo com balão de fala aberto na tela) são recarregados instantaneamente em menos de 1 ms a 30 FPS sem congelamentos, deadlocks ou necessidade de reiniciar o jogo.
  - **Persistência Automática:** A preferência de idioma é salva em `hl_settings.ini` e preservada entre sessões.
- **Cloud Save & Sincronização Cruzada (Google Drive RFC 8628 Device Flow):**
  - Salve seu progresso no PC e continue no Celular ou Nintendo Switch via backup em nuvem integrado.
  - Autenticação sem navegador embutido via código de dispositivo (`google.com/device`).
  - Organização automática em pasta dedicada **"Heroes Lore - Wind of Soltia"** no Google Drive, isolando os saves e mantendo a raiz limpa.
  - Empacotamento atômico em JSON de todos os 3 heróis da campanha (Ronin, Reah, Aramor) e progresso global.
- **Engine de Tipografia Vetorial TrueType (stb_truetype + Inter-SemiBold):**
  - Renderização tipográfica de alta definição com antialiasing linear suave e kerning proporcional, substituindo a antiga fonte bitmap pixelada nos modais de Cloud Save e Auto-Updater.
  - Suporte completo a caracteres acentuados da língua portuguesa (á, é, í, ó, ú, ç, ã, õ).
- **Proporção Automática Inteligente (Aspect Auto):**
  - O motor detecta a orientação do dispositivo/janela em tempo real: aplica **16:9 True Widescreen** em modo paisagem e **3:4 Original com Molduras** em modo retrato automaticamente.
- **Fidelidade Matemática e Lógica 1:1:** O jogo executa a partir dos binários `.class` originais do JAR, garantindo que a física, colisões, progressão de nível e IA de chefes sejam idênticos ao jogo original de celular.
- **True Widescreen (16:9 — 568x320):** Expansão real da área de visão do mapa, exibindo até **35 colunas de tiles simultaneamente** (em comparação com 15 colunas do original de 240p), pixel-perfect e sem qualquer distorção anamórfica de sprites.
- **Molduras Temáticas Clássicas (Bezels):** Para quem prefere a nostalgia da proporção 3:4 original, estão disponíveis artes laterais dedicadas (*Soltia Ancestral*, *Ardósia Escura* e *Preto Clássico*).
- **Limitador de Taxa de Quadros Preciso:** Alterne instantaneamente entre **15 FPS** (cadência clássica J2ME com frame pacer preciso) e **30 FPS Turbo** (movimentação e combates com fluidez aprimorada), diretamente pelo Menu de Opções in-game ou atalhos.
- **HUD On-Screen Display (OSD):** Banner informativo flutuante estilo console moderno com visual em vidro fosco (*frosted glass*) que confirma as alterações em tempo real.
- **Sintetizador MIDI Nativo & Trilha Sonora Completa Restaurada:** Trilhas sonoras MIDI executadas em tempo real utilizando TinySoundFont (`tsf`) com o banco orquestrado SoundFont `TimGM6mb` embutido, mixadas simultaneamente com efeitos sonoros `.wav` de combate. Inclui todas as **12 músicas de fundo de mapas** restauradas da versão original (`ZH v2.0.7 K700`) que haviam sido cortadas do JAR PT-BR (Cidades 1–4, Casas/Tavernas, Florestas, Campos de Neve, Castelos, Cavernas, Templos e Ruínas).
- **Persistência Completa (Save & Load):** Suporte nativo ao subsistema J2ME RMS (*Record Management System*) com gravação e leitura em tempo real dos 3 slots de personagens (`_k`, `_s`, `_w`) e estado global (`_o`), além de salvar configurações em `hl_settings.ini`.
- **Controles Modernizados:** Suporte plug-and-play para controles de Xbox, PlayStation, Switch Pro Controller, teclado de PC (WASD + Espaço ou Teclado Numérico) e touchscreen capacitivo.
- **Vibração Háptica (Rumble):** Feedback de impacto nos ataques e habilidades em controles compatíveis (Joy-Cons, DualSense, Xbox Controller).

---

## 🎮 Controles no Teclado, Gamepad e Touchscreen

O mapeamento foi planejado com ergonomia moderna para controles USB/Bluetooth, teclado e tela sensível ao toque, mantendo compatibilidade total com os atalhos originais do celular J2ME:

### 🕹️ Gamepad (Xbox, PlayStation, Switch Pro, 8BitDo, Steam Deck)

| Ação no Jogo | Controle Xbox / Steam Deck | Controle PlayStation | Controle Switch Pro / Joy-Con |
| :--- | :--- | :--- | :--- |
| **Movimentação (360° Contínua)** | <kbd>D-Pad</kbd> ou <kbd>Analógico Esquerdo</kbd> | <kbd>D-Pad</kbd> ou <kbd>Analógico Esquerdo</kbd> | <kbd>D-Pad</kbd> ou <kbd>Analógico Esquerdo</kbd> |
| **Atacar (Arma) / Confirmar ('5')** | <kbd>A</kbd> | <kbd>✕</kbd> | <kbd>A</kbd> |
| **Status / Cancelar (RSK)** | <kbd>B</kbd> | <kbd>○</kbd> | <kbd>B</kbd> |
| **Menu Principal / Inventário (CLR)** | <kbd>Start</kbd> *(isolado)* | <kbd>Options</kbd> *(isolado)* | <kbd>+</kbd> *(isolado)* |
| **Abrir / Fechar Minimapa ('0')** | <kbd>Select</kbd> *(toque rápido)* ou <kbd>L3</kbd> | <kbd>Share/Touchpad</kbd> *(toque rápido)* ou <kbd>L3</kbd> | <kbd>-</kbd> *(toque rápido)* ou <kbd>L3</kbd> |
| **Ataque 1 do Guardião ('1')** | <kbd>X</kbd> | <kbd>□</kbd> | <kbd>X</kbd> |
| **Ataque 2 do Guardião ('3')** | <kbd>Y</kbd> | <kbd>△</kbd> | <kbd>Y</kbd> |
| **Ataque Secundário / Habilidade ('7')**| <kbd>LB</kbd> (L1) | <kbd>L1</kbd> | <kbd>L</kbd> |
| **Usar Poção / Item Rápido ('9')** | <kbd>RB</kbd> (R1) | <kbd>R1</kbd> | <kbd>R</kbd> |
| **Alternar Poção $\leftarrow$ Anterior**| <kbd>LT</kbd> (Gatilho Esquerdo) | <kbd>L2</kbd> | <kbd>ZL</kbd> |
| **Alternar Poção $\rightarrow$ Seguinte**| <kbd>RT</kbd> (Gatilho Direito) | <kbd>R2</kbd> | <kbd>ZR</kbd> |
| 🖥️ **Proporção (3:4 <-> 16:9 Widescreen)**| <kbd>Select</kbd> + <kbd>Start</kbd> | <kbd>Share</kbd> + <kbd>Options</kbd> | <kbd>-</kbd> + <kbd>+</kbd> |
| 🖼️ **Moldura (Soltia / Ardósia / Preto)**| <kbd>Select</kbd> + <kbd>R3</kbd> | <kbd>Share</kbd> + <kbd>R3</kbd> | <kbd>-</kbd> + <kbd>R3</kbd> |
| ⚡ **Velocidade (15 FPS <-> 30 FPS Turbo)**| <kbd>R3</kbd> *(clique isolado)* | <kbd>R3</kbd> *(clique isolado)* | <kbd>R3</kbd> *(clique isolado)* |
| 📳 **Vibração / Haptics** | Rumble dinâmico | Rumble dinâmico | Rumble dinâmico |

---

### ⌨️ Teclado de Computador (PC / Linux)

| Ação no Jogo | Teclas Modernas (PC) | Teclado Numérico (J2ME) | Tecla Original |
| :--- | :--- | :--- | :--- |
| **Mover para Cima** | <kbd>W</kbd> ou <kbd>↑</kbd> | <kbd>Num 2</kbd> | `UP` / `2` |
| **Mover para Baixo** | <kbd>S</kbd> ou <kbd>↓</kbd> | <kbd>Num 8</kbd> | `DOWN` / `8` |
| **Mover para a Esquerda** | <kbd>A</kbd> ou <kbd>←</kbd> | <kbd>Num 4</kbd> | `LEFT` / `4` |
| **Mover para a Direita** | <kbd>D</kbd> ou <kbd>→</kbd> | <kbd>Num 6</kbd> | `RIGHT` / `6` |
| **Atacar (Arma) / Confirmar / Interagir** | <kbd>Espaço</kbd>, <kbd>Enter</kbd>, <kbd>J</kbd>, <kbd>Z</kbd> | <kbd>Num 5</kbd> | `FIRE` / `5` |
| **Menu Principal / Inventário** | <kbd>Tab</kbd>, <kbd>Esc</kbd>, <kbd>F1</kbd>, <kbd>C</kbd> | — | `CLR` (-8) |
| **Status / Cancelar / Fechar** | <kbd>Backspace</kbd> | — | `RSK` (-7) |
| 🌐 **Alternar Idioma (PT-BR, EN, IT, ES)** | <kbd>F2</kbd> | — | — |
| **Ataque 1 do Guardião** | <kbd>Q</kbd> ou <kbd>U</kbd> | <kbd>Num 1</kbd> | `1` |
| **Ataque 2 do Guardião** | <kbd>E</kbd> ou <kbd>I</kbd> | <kbd>Num 3</kbd> | `3` |
| **Ataque Secundário / Habilidade** | <kbd>K</kbd> ou <kbd>X</kbd> | <kbd>Num 7</kbd> | `7` |
| **Usar Poção / Item Rápido** | <kbd>L</kbd> ou <kbd>V</kbd> | <kbd>Num 9</kbd> | `9` |
| **Poção Anterior (Esquerda)** | <kbd>[</kbd> ou <kbd>,</kbd> | — | `35` (3×) |
| **Próxima Poção (Direita)** | <kbd>]</kbd>, <kbd>.</kbd> ou <kbd>#</kbd> | <kbd>#</kbd> (Numpad) | `#` |
| **Abrir/Fechar Minimapa** | <kbd>M</kbd>, <kbd>R</kbd>, <kbd>O</kbd> ou <kbd>0</kbd> | <kbd>Num 0</kbd> | `0` |
| **Atalhos Rápidos** | <kbd>*</kbd> | <kbd>*</kbd> (Numpad) | `*` |
| 🖼️ **Alternar Moldura (Bezel)** | <kbd>F5</kbd> | — | — |
| ⚡ **Alternar FPS (15 <-> 30 Turbo)** | <kbd>F6</kbd> ou <kbd>F8</kbd> | — | — |
| 🖥️ **Alternar Widescreen (3:4 <-> 16:9)** | <kbd>F7</kbd> ou <kbd>F10</kbd> | — | — |
| 🔄 **Checar Atualização (OTA In-App)** | <kbd>F9</kbd> ou Menu Sobre (<kbd>5</kbd> / <kbd>A</kbd>) | — | — |
| 📺 **Tela Cheia (Fullscreen)** | <kbd>F11</kbd> ou <kbd>Alt + Enter</kbd> | — | — |

---

### 📱 Controles Virtuais Touchscreen (Celular Android / Nintendo Switch)

Na tela sensível ao toque, a interface translúcida estilo *frosted glass* oferece ergonomia de console sem poluir o jogo:
- **Modo Vertical (Retrato em Smartphones / TATE no Switch):**
  - **Tela do Jogo no Topo Absoluto:** O jogo inicia no topo da tela (com margem sutil de 1.5% para notch/status bar), liberando mais de 900px na base em smartphones compridos (20:9).
  - **Zero Sobreposição:** O jogo, a barra de HP/SP e os balões de diálogo dos NPCs ficam 100% desobstruídos no topo, enquanto todos os controles virtuais operam na porção inferior.
  - **Pílulas de Sistema [ MENU ] e [ MAPA ] ao Alcance dos Dedos:** Reposicionadas confortavelmente logo acima da borda superior do D-Pad, acessíveis pelo polegar esquerdo sem exigir soltar o aparelho para alcançar o topo da tela.
  - **Cluster de Ação em Arco Ergonômico:** Formato clássico preservado com **5** (Ataque/Confirmar), **1** e **3** (Skills), **7** (Poção), **9** (Item) e o botão circular **R BACK** na direita-baixo para cancelar diálogos, fechar menus e abrir status.
- **Base da Tela:**
  - **Botão Olho (`👁️`):** Oculta ou reexibe os botões na tela (ideal para jogar com gamepad ou assistir cutscenes limpas).
  - **Botão Rotação (`🔄`, no Switch):** Alterna entre orientação horizontal (Paisagem 1280x720) e vertical (Retrato TATE 90° e 270° Flip Grip).
  - **Setas de Poção (`◀` e `▶`):** Centralizadas na base da tela para troca rápida de poções sem desviar o polegar do combate.
- **Ajustes de Tela e FPS:** Migrados diretamente para o **Menu de Opções in-game (`be.class`)**, com suporte ao modo **Aspect Auto** (detecta orientação do aparelho automaticamente) e atalhos rápidos de gamepad/teclado.

---

## 🕹️ Funcionalidades Específicas por Plataforma

### 🎮 Nintendo Switch (.nro Homebrew)
- **Instalação:** Copie `heroes_lore.nro` para `sdmc:/switch/heroes_lore/heroes_lore.nro` no seu Switch desbloqueado (Atmosphere).
- **Detecção Automática do Modo TV (Dock):** Consulta nativa via `libnx` (`appletGetOperationMode()`). Ao conectar no dock para jogar na TV em 1080p com Joy-Cons ou Pro Controller, todos os controles virtuais touch e rotações são automaticamente desativados, garantindo visualização cinematográfica limpa com mensagens OSD informativas.
- **3 Modos de Rotação da Tela (Orientação Dinâmica em Modo Portátil):**
  - **Modo 0 (Paisagem Padrão - 1280x720):** Jogo centralizado na proporção clássica 240x320 com barras decorativas laterais.
  - **Modo 1 (Retrato 90° Horário):** Segure o console na vertical como um smartphone gigante.
  - **Modo 2 (Retrato 270° Anti-horário / Flip Grip):** Otimizado especialmente para acessórios como o **Flip Grip**, posicionando os Joy-Cons confortavelmente nas laterais.
- **Modais Nativos em Modo Retrato (TATE):** As janelas modais de **Cloud Save** e **Atualizador OTA** são renderizadas diretamente na textura vertical rotacionada (720x1280), adaptando-se com perfeita legibilidade sem travamentos.
- **Alternar Orientação:** Pressione <kbd>-</kbd> (Minus) a qualquer momento para ciclar entre os modos.
- **Ocultar/Reexibir Botões Virtuais:** Pressione <kbd>L3</kbd> (pressionar o analógico esquerdo).
- **Touchscreen Capacitivo:** Toques na tela capacitiva do Switch são mapeados matematicamente em 1:1, mesmo com a tela rotacionada.

### 🐧 Linux & Steam Deck (.AppImage)
- **Execução Direta:** Pacote AppImage portátil que não requer instalação de dependências:
  ```bash
  chmod +x heroes_lore_linux_x86_64.AppImage
  ./heroes_lore_linux_x86_64.AppImage
  ```
- **Steam Deck (SteamOS):** No modo Desktop, adicione o arquivo como "Jogo não-Steam" à sua biblioteca. O jogo roda diretamente no modo Gaming com suporte completo aos controles embutidos do portátil!

### 📱 Android (APK Nativo Standalone)
- **Instalação Direta:** Baixe o APK release da seção de Releases e instale no seu celular ou tablet.
- **Controles na Tela:** D-Pad virtual analógico e botões de ação com feedback tátil e suporte a multitoque suave.
- **Controles Bluetooth/USB:** Plug-and-play imediato com controles de Xbox, PlayStation, Gamesir e Razer Kishi.

---

## 🏗️ Arquitetura do Projeto

O código-fonte foi estruturado de forma desacoplada em módulos nativos limpos:

```
heroes_lore_recomp/
├── src/
│   ├── vm/                     # Motor de Execução JVM / CLDC 1.1 em C++17
│   │   ├── vm.h / vm.cpp       # Gerenciamento de classes, pool de constantes, GC com tracing de raízes
│   │   ├── interp.cpp          # Interpretador de bytecodes otimizado (focado nas instruções do jogo)
│   │   └── natives.cpp         # Implementação das APIs nativas de java/lang, java/io e java/util
│   ├── midp/                   # Emulação da camada J2ME MIDP 2.0
│   │   ├── midp.h / midp.cpp   # Displayable, Canvas, Graphics 2D, Image e RecordStore (RMS)
│   │   └── stb_image.h         # Decodificação de imagens PNG e texturas
│   ├── platform/               # Camada de Plataforma (Hardware Abstraction Layer)
│   │   ├── platform.h          # Interface de janela, loop de eventos e áudio
│   │   ├── platform_sdl.cpp    # Backend SDL2 com renderização acelerada, escala e entrada
│   │   ├── midi_synth.h        # Interface do sintetizador de áudio
│   │   └── midi_synth.cpp      # Sintetizador MIDI com TinySoundFont (TSF) + TinyMidiLoader (TML)
│   └── main.cpp                # Ponto de entrada, boot do GameMIDlet e loop principal
├── reference/
│   ├── extracted/              # Assets extraídos do JAR (paletas, fontes, mapas, sons, classes)
│   ├── decompiled_cfr/         # 90 classes originais decompiladas com CFR para auditoria
│   └── javap/                  # Bytecode original desmontado com javap
├── dist_linux/                 # Metadados de desktop, ícones e AppRun para empacotamento Linux
├── switch/                     # Ícone oficial e assets específicos do Nintendo Switch
├── android/                    # Projeto Gradle + NDK para compilação do APK Android
├── tools/                      # Scripts auxiliares de empacotamento, créditos e RomFS
├── docs/                       # Documentação técnica detalhada, mapa de classes e learnings
└── CMakeLists.txt              # Configuração de build moderna para CMake e Ninja
```

---

## ⚙️ Como Compilar o Projeto

O projeto utiliza um pipeline unificado no GitHub Actions que compila automaticamente todas as plataformas a cada push ou tag. Se preferir compilar localmente:

### 🪟 Windows (MSYS2 UCRT64)
```powershell
# Pré-requisitos: pacotes mingw-w64-ucrt-x86_64-gcc, cmake, ninja, SDL2 e SDL2_mixer
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\heroes_lore.exe
```

### 🐧 Linux (Ubuntu / Debian / Arch / Fedora)
```bash
# Pré-requisitos: build-essential, cmake, ninja-build, libsdl2-dev, libsdl2-mixer-dev, pkg-config
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 tools/prepare_appimage.py
```

### 🎮 Nintendo Switch (devkitA64 + libnx)
```bash
python3 tools/prepare_switch_romfs.py
make -f Makefile.switch
# Gera: heroes_lore.nro
```

### 📱 Android (Gradle + NDK)
```bash
cd android
./gradlew assembleRelease
# Gera os APKs em android/app/build/outputs/apk/release/
```

---

## 📜 Engenharia Reversa & Documentação Técnica

Para detalhes sobre a engenharia reversa das 90 classes ofuscadas, resolução de colisões de identificadores no carregador de classes e detalhes de implementação do RMS, consulte:
- [`docs/STATUS.md`](docs/STATUS.md) — Marco atual e checklist de desenvolvimento.
- [`docs/LEARNINGS.md`](docs/LEARNINGS.md) — Aprendizados técnicos, armadilhas superadas e decisões de arquitetura.
- [`docs/VERSIONING.md`](docs/VERSIONING.md) — Guia de versionamento multi-plataforma e lançamento de releases.
- [`docs/CLASS_MAP.md`](docs/CLASS_MAP.md) — Mapeamento detalhado das classes do jogo.
- [`docs/PORTING_GUIDE_BLUEPRINT.md`](docs/PORTING_GUIDE_BLUEPRINT.md) — Guia Mestre e Blueprint Arquitetural para portabilidade de jogos J2ME.

---

## 👥 Créditos & Agradecimentos

- **Port Nativo & Recompilação (Windows, Linux, Android, Nintendo Switch):** [David Kalil Braga](https://github.com/davidkalil10) (2026)
- **Desenvolvimento Original J2ME:** *Hands-On Mobile* & *Electronic Arts (EA Mobile)*
- **Localizações e Traduções:**
  - **Português-BR (J2ME Original):** *Open Mind Team* (Bruno Freire, Bruno Vilhena, John Peres) — Versão `v.0.0.2` (2008)
  - **Inglês:** Versão de varejo original Nokia / Hands-On Mobile (2008)
  - **Italiano:** Versão de varejo oficial S60v3 / BiNPDA (2008)
  - **Espanhol:** Localização completa de diálogos e textos adaptada para o recompilador nativo (2026)

---

## ⚖️ Licença e Aviso Legal

Este projeto é uma obra de preservação histórica de software e engenharia reversa educacional. Todas as marcas registradas, títulos, gráficos e áudios originais de *Heroes Lore: Wind of Soltia* pertencem à *Hands-On Mobile* e *Electronic Arts (EA)*.
