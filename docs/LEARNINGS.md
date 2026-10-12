# LEARNINGS

## Herdado das versões Flutter (v1/v2)
Fonte: `heroes_lore_modern/PROJECT_KNOWLEDGE_BACKUP.md`

**Funcionou**
- Formatos de asset do JAR: `.map`, `.evt`, `.mpd`, `.mph`; parsers Python existem na v2 (`map_evt_parser.py`).
- Objetos/NPCs ancorados em BOTTOM_HCENTER; posição = `(x*16 + param1, y*16 + param2)` em pixels (tile 16px; v2 escalava 2x).
- Z-order por Y em pixels (maior Y desenha por cima).
- NPC `type_idx < 18` = herói (pasta própria); `>= 18` = aldeão em `/npc/all`, arquivo `type_idx - 18`.
- Dump de diálogos e `lang` já extraídos (`dialogos_completos.json`, `lang.en-GB`).

**Deu errado / limitação**
- A lógica foi recriada com base em engenharia reversa parcial e observação visual → divergências. Por isso o recomp agora é literal a partir do código decompilado.
- Movimento em grid na v2 divergia do original (que usa movimento por pixel); decidir só após ler o código real.

**Ambiente**
- Sistema de permissões do IDE bloqueia leitura fora do workspace até o usuário liberar cada pasta explicitamente.

## Sessão 1 (decompilação)
- **PowerShell:** nomes de arquivo com colchetes (`[BR]`) quebram Copy-Item por serem curinga. Usar `-LiteralPath`. (Expand-Archive só aceita .zip: copiar o .jar como .zip.)
- **Permissões do IDE:** pastas fora do workspace são negadas sem prompt. Solução: copiar os projetos para `dados de consulta/` dentro do workspace.
- **CFR:** usar `--renamedupmembers true --renameillegalidents true`; o código ofuscado tem campos/métodos com mesmo nome e tipos diferentes (`a`), ambíguos sem isso. Gera nomes como `var_int_a`.
- **CFR falha em `ao`, `g`, `n`** (`Loose catch block` em `a()` e `a(byte[])`): usar `reference/javap/*.txt` como fonte de verdade nesses métodos.
- JAR: 90 classes, só `rpg.GameMIDlet` tem pacote. MIDP-2.0/CLDC-1.1, jar de 240x320. Props: HO-LSK/HO-RSK/HO-CLR (softkeys).
- Há 3 JARs além do 240x320: variantes S60v3 (320x240 v2.0.5 e 352x416 v0.0.2). O arquivo `[BR].jar` tem o mesmo tamanho (813744) do `[BR]240x320.jar`.

- **Toolchain (instalada via winget MSYS2.MSYS2):** MSYS2 em `C:\msys64`, ambiente **UCRT64**: g++ 16.2, CMake 4.4, Ninja, gdb 18, SDL2 2.32.10, SDL2_mixer 2.8.2 (também há SDL3 3.4.18). Para usar no PowerShell: `\$env:Path = 'C:\msys64\ucrt64\bin;' + \$env:Path`. `bash -lc` abre no ambiente MSYS (sem g++); usar o PATH do ucrt64. `sdl2-config` não existe: usar `find_package(SDL2)` ou `-lmingw32 -lSDL2main -lSDL2`.
- O 1º `pacman -Syu` fecha o shell ao atualizar o runtime (esperado): rodar de novo.
- Headers SDL2 ficam em `ucrt64/include/SDL2`: compilar com `-IC:/msys64/ucrt64/include/SDL2` (ou `find_package(SDL2)` no CMake). Smoke test SDL2+SDL2_mixer compilou e rodou OK.

## Sessão 2 (Interpretador CLDC 1.1 + Runtime MIDP 2.0 Nativo)
- **Colisão de Campos Ofuscados:** O ofuscador J2ME cria múltiplos campos com o mesmo identificador na mesma classe, diferenciados apenas pelo descritor de tipo (ex: na classe `bs`, `a` existe como `byte`, `int[]`, `Display` e `r`). A tabela de campos `fieldMap` e a resolução de bytecode (`getfield`, `putfield`, `getstatic`, `putstatic`) **obrigatoriamente** devem indexar por `nome:descritor`.
- **Herança de Classes Built-in:** Subclasses de classes nativas (como `Exception` estendendo `Throwable`) devem herdar `super->nInst` e os descritores de referência para que instâncias de exceção tenham slots de memória adequados.
- **Multithreading J2ME e GIL:** O jogo usa threads ativas (ex: `bs.run()`, carregamento de recursos assíncronos) combinadas com `Display.callSerially()`. A GIL (Global Interpreter Lock) da VM com liberação atômica em `Thread.sleep()` e `Thread.yield()` garante concorrência perfeita e sincronização limpa com o loop SDL.
- **Renderização e Áudio:** O SDL2 seleciona automaticamente Direct3D11 e WASAPI no Windows. O MIDP LCDUI renderiza o framebuffer 240x320 escalado com preservação de aspect ratio e filtragem pixel-art (nearest neighbor).
- **Fidelidade Visual 100%:** A tela de splash (logo EA Mobile), a tela de título animada com pétalas azuis e espada dourada ("Aperta '5'", versão 0.0.2 em PT-BR) e o menu principal em pergaminho ("INICIAR", "CARREGAR", "OPCOES", "INFO", "SOBRE", "SAIR" com destaque carmesim) rodam perfeitamente a partir do bytecode original sem qualquer alteração na lógica do jogo.
- **Dependências de DLLs no Windows:** O `SDL2_mixer.dll` no MSYS2 UCRT64 requer codecs de áudio externos (`libFLAC.dll`, `libmpg123-0.dll`, `libopusfile-0.dll`, `libvorbisfile-3.dll`, `libwavpack-1.dll`, `libxmp.dll`, `libogg-0.dll`, `libopus-0.dll`, `libvorbis-0.dll`), além de `libgcc`, `libstdc++` e `libwinpthread`. Todas foram copiadas para `build/` e configuradas no `POST_BUILD` do `CMakeLists.txt` para execução autônoma (portable/standalone).
- **Direct3D 11 vs Direct3D 9 em SDL2:** O backend D3D9 padrão do SDL2 no Windows pode falhar silenciosamente no streaming contínuo de texturas com `SDL_UpdateTexture`/`SDL_RenderCopy`. Forçar `SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11")` e usar `SDL_LockTexture`/`memcpy` linha a linha com `SDL_RenderSetLogicalSize` garante renderização pixel-perfect e zero quedas de frames em todas as placas de vídeo.

## Sessão 3 (Salvar Jogo / Streams Polimórficos / RMS)
- **Polimorfismo de Streams J2ME (`java/io/OutputStream` vs `FilterOutputStream` / `DataOutputStream`):**
  - No salvamento in-game (`ah.f()` -> `n.o()`), o código chama `((OutputStream)filterOutputStream).write(byte_array)` onde `filterOutputStream` é um `java/io/DataOutputStream` (`Dos*`) empacotando um `java/io/ByteArrayOutputStream` (`Baos*`).
  - No despacho de método virtual (`invokevirtual`), o método chamado era resolvido como `java/io/OutputStream.write:([B)V`. A implementação nativa anterior fazia `static_cast<Baos*>(args[0].o)`, o que é fatal pois o objeto passado como `this` era o `Dos*`.
  - Como o layout de memória de `Dos` (`Object* out`) difere do layout de `Baos` (`std::vector<uint8_t> data`), isso causava corrupção de ponteiro e `Segmentation Fault / Access Violation` imediato no momento em que o jogador clicava em Salvar no menu.
  - **Solução:** Unwrapping recursivo universal `getUnderlyingBaos(vm, out)` que desembala camadas de `FilterOutputStream`/`DataOutputStream` até o buffer de memória real subjacente, acompanhado do registro de todas as variantes de `write([B)`, `write([BII)`, `write(I)`, `writeByte`, `writeShort`, `writeInt`, `writeUTF`, `flush`, `reset` e `close` em todas as classes da hierarquia de I/O.
- **Persistência RMS:**
  - O formato J2ME RMS armazena registros indexados em base 1.
  - A rotina de salvamento do jogo abre os RecordStores `_k`, `_s`, `_w` e `_o`, deleta cópias antigas e reinsere os buffers comprimidos/criptografados via `au.void_a()`.
  - Tratamento defensivo de registros de tamanho zero (`len == 0`) e suporte a `setRecord` garantem integridade total do ciclo de vida dos arquivos salvos na pasta `data/rms/`.

## Sessão 4 (Movimentação Contínua Fluida / Typematic Delay)
- **Mecânica de Caminhada J2ME (`as.java`, `r.java`, `n.java`):**
  - Nos celulares J2ME originais com teclado físico, manter uma tecla direcional pressionada gerava uma torrente contínua de eventos `keyPressed` pelo firmware do aparelho.
  - No código do jogo (`n.java`), o primeiro `keyPressed` inicia a caminhada (`n.c(2)`). Cada passo leva um certo número de ticks (16 pixels). Se novos eventos `keyPressed` chegam enquanto o herói caminha, `n.b()` seta `var_byte_h = 2` (indicador de "manter caminhada"). Ao atingir a borda do tile, `n.q()` verifica `var_byte_h`: se for 2, continua andando imediatamente; se for 0, o herói para no lugar!
  - Anteriormente, o SDL descartava teclas repetidas com `&& !ev.key.repeat`. Isso fazia com que, ao segurar uma tecla, apenas 1 evento fosse entregue: o herói dava um passo e parava.
  - **Solução implementada:** Criado em `src/platform/platform_sdl.cpp` um sistema de rastreamento físico contínuo via `SDL_GetKeyboardState`. Aplicado um delay inicial de 160ms (para que toques rápidos no menu não disparem múltiplos eventos acidentais) seguido de repetição suave a cada 40ms (~25 Hz).
  - Ao soltar a tecla, `keyReleased` é disparado imediatamente, garantindo paradas precisas nas bordas dos tiles. Transições entre direções funcionam instantaneamente.

## Sessão 5 (Suporte Completo a Gamepads / SDL_GameController)
- **Integração Plug-and-Play:**
  - Inicialização do subsistema `SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC` com varredura automática no boot e listeners para `SDL_CONTROLLERDEVICEADDED` e `SDL_CONTROLLERDEVICEREMOVED`.
  - Mapeamento universal compatível com controles XInput (Xbox 360 / One / Series), DirectInput, DualShock 4 / DualSense e Nintendo Switch Pro.
- **Unificação de Movimento (D-Pad + Analógico Esquerdo):**
  - O estado contínuo de direção consulta simultaneamente o teclado e os gamepads conectados (`getGamepadHeldDirection()`).
  - O analógico esquerdo com deadzone de 12.000 unidades alimenta o mesmo loop typematic suave (160ms inicial / 40ms repetição), permitindo navegar no analógico sem solavancos.
- **Gatilhos Analógicos e Atalhos:**
  - Gatilho Direito (RT/R2): Dispara ataque/ação ('5').
  - Gatilho Esquerdo (LT/L2) e L3: Uso rápido de poções ('0').
  - Botões de Face e Ombro (A, B, X, Y, L1, R1, R3, Start, Back) cobrem 100% dos botões originais do J2ME.
  - Implementado `Platform::rumble(...)` para suporte a vibração háptica.

## Sessão 6 (Port Nativo para Android / Standalone APK)
- **Acesso a Recursos dentro do APK (AAssetManager vs Arquivos Tradicionais):**
  - No Android, os assets empacotados dentro do APK não existem como arquivos descompactados no sistema de arquivos comum do Linux. Chamadas como `fopen("META-INF/MANIFEST.MF")` ou `std::ifstream` falham silenciosamente.
  - A solução arquitetural ideal foi unificar todo o acesso de assets através de `Platform::readAsset(path)` utilizando `SDL_RWFromFile()`. O SDL2 possui integração nativa com o `AAssetManager` do NDK, permitindo ler classes `.class`, manifestos, imagens `.png` e músicas `.mid` diretamente de dentro do APK compactado sem descompactação prévia.
  - Para persistência de `RecordStore` (saves RMS), `Platform::getStorageDir()` utiliza `SDL_AndroidGetInternalStoragePath()`, gravando os dados no diretório privado seguro do app (`/data/data/org.libsdl.app/files/rms/`).
- **Configuração do `SDL2_mixer` para Android via CMake:**
  - O `SDL2_mixer` depende de bibliotecas externas caso ativadas cegamente. No Android, para evitar dependências de terceiros não instaladas (WavPack, GME, Fluidsynth, Opus), configurou-se `SDL2MIXER_VENDORED=ON`, ativando o sintetizador MIDI Timidity embutido (`SDL2MIXER_MIDI_TIMIDITY=ON`), decodificador STB Vorbis (`SDL2MIXER_VORBIS_STB=ON`) e suporte nativo a WAV (`SDL2MIXER_WAVE=ON`).
- **Ciclo de Vida do Entry Point no Android (`libmain.so`):**
  - O `SDLActivity.java` carrega `libSDL2.so`, `libSDL2_mixer.so` e `libmain.so`.
  - A função `nativeRunMain` do SDL localiza dinamicamente a função `SDL_main` via `dlsym`. Com a inclusão de `<SDL_main.h>`, `main` é automaticamente exportado com linkage `extern "C"` sem mangling de C++.
- **Controles Virtuais Touchscreen (Multi-touch):**
  - Dispositivos móveis sem gamepad físico necessitam de controles na tela. Foi implementado em `src/platform/platform_sdl.cpp` um sistema de overlay translúcido minimalista renderizado diretamente no canvas com `SDL_BLENDMODE_BLEND`.
  - Eventos `SDL_FINGERDOWN`, `SDL_FINGERMOTION` e `SDL_FINGERUP` fornecem suporte a multi-touch verdadeiro (o jogador pode manter o polegar esquerdo no D-Pad virtual enquanto pressiona ataques e habilidades com o polegar direito).
  - Em telas ultrawide com barras pretas nas laterais (letterbox no modo paisagem), toques nos pilares pretos são detectados e mapeados ergonomicamente (D-pad na coluna esquerda e botões na coluna direita).
- **Tamanho e Eficiência:**
  - O APK final gerado (`bin/heroes_lore.apk`) possui apenas ~3.68 MB e inclui todas as 996 classes e assets do jogo, além das bibliotecas nativas completas compiladas para 64-bit (`arm64-v8a`) e 32-bit (`armeabi-v7a`).

## Sessão 7 (Redesign do Gamepad Virtual e Ícones Nativos em Alta Resolução)
- **Causa Raiz do Deslocamento de Toque (Offset Touch) no Android:**
  - O uso anterior de `SDL_RenderSetLogicalSize(s_renderer, 240, 320)` forçava a GPU a mapear todo o espaço de renderização em uma caixa lógica 240x320 centralizada na tela com letterbox. Quando coordenadas de toque (`ev.tfinger`) eram passadas por `SDL_RenderWindowToLogical`, as transformações sofriam distorções de proporção em telas modernas 20:9 e 19.5:9, deslocando a área de clique para longe do botão visual.
  - Além disso, os botões eram desenhados por cima do jogo (obscurecendo o herói, inimigos e caixas de diálogo).
- **Arquitetura de Gamepad Estilo Emulador Moderno (Fullscreen Frosted Glass Overlay):**
  - O jogo agora ocupa o tamanho máximo possível da tela mantendo o aspect ratio original (240x320) perfeitamente centralizado.
  - Os botões virtuais e o D-Pad flutuam diretamente por cima do jogo com modulação de transparência alfa (`SDL_SetTextureAlphaMod`):
    - Em repouso: opacidade translúcida a ~60% (155/255), permitindo que o mapa, inimigos e textos passem por baixo sem bloquear a visão.
    - Ao tocar: o botão pressionado atinge 100% de opacidade e ativa o halo neon ciano (`btn_glow`), além da seta brilhante indicando a direção no D-Pad.
    - Adicionadas sombras projetadas (drop shadow) nas fontes dos botões para legibilidade impecável sobre qualquer tipo de cenário (neve, caverna, grama ou pergaminho).
  - Coordenadas de toque são calculadas diretamente em pixels de tela física (`ev.tfinger.x * winW`, `ev.tfinger.y * winH`), garantindo precisão 1:1 absoluta. A margem de conforto tátil foi estendida para 1.30x no raio dos botões para eliminar completamente toques perdidos.
- **Pipeline de Texturas com Supersampling (4x AA):**
  - Desenvolvido gerador gráfico em Python (`tools/generate_gamepad_textures.py`) utilizando PIL com renderização em 4x supersampling (Lanczos) para produzir botões com estética glassmorphic moderna: anéis escuros com chanfro de luz e sombra 3D (bevel), ícones iluminados e feedback neon cyan (`btn_glow`) quando pressionados.
  - Texturas são carregadas em formato cru RGBA (`.rgba`) diretamente pela interface `Platform::readAsset` e instanciadas em superfícies SDL (`SDL_PIXELFORMAT_RGBA32`).
- **Ícones Personalizados Oficiais em Mipmaps:**
  - Extraído o asset oficial `logo 512.png` fornecido pelo usuário em `C:\Users\david\Downloads\logo 512.png` (copiado para `assets/logo_512.png`).
  - Gerados ícones para todas as densidades Android (`mdpi`, `hdpi`, `xhdpi`, `xxhdpi`, `xxxhdpi`) em versões padrão e circular (`ic_launcher_round.png`), com registro no `AndroidManifest.xml`.
- **Refinamento de Proporção, Disposição Diamante e Modo Paisagem:**
  - `MENU`, `MAPA` e `R` (substituindo RSK por R para simplicidade e familiaridade com controles de videogame) foram remodelados com textura de alta resolução 240x100 em proporção 2.4:1 (evitando o achatamento elíptico) e posicionados com recuo de 8% da altura da tela para nunca colidirem com o relógio/notch do sistema.
  - No Modo Paisagem (Horizontal), todos os controles nas colunas pretas laterais foram redimensionados para tamanhos 2x a 3x maiores (`MENU` 240x100, `MAPA` e `R` 200x83, setas 140px, cluster de ação amplo), preenchendo as laterais de forma equilibrada e ergonômica.

## Sessão 8 (Restauração da Ergonomia Clássica, Elevação dos Controles e Hit-Testing por Menor Distância)
- **Ergonomia do Polegar Humano vs. Diamante Simétrico Ortogonal:**
  - O layout em cruz/diamante estrito (N/S/L/O) foi rejeitado no teste prático porque o polegar direito não se move em ângulos cardeais perfeitos; ele pivota a partir da articulação metacárpica descrevendo um arco natural com inclinação de ~30° a 45°.
  - A disposição clássica original (5 no centro para ataque, 1 e 7 na coluna esquerda, 3 acima de 5 e 9 à direita) encaixa perfeitamente no repouso do dedo.
- **Elevação Sincronizada do D-Pad e Bloco de Ação:**
  - Ao invés de amontoar as setas de troca de poção entre os botões de ação ou distorcer os botões, a solução ideal foi elevar em sincronia tanto o D-Pad quanto todo o cluster de ação para ~42% da altura em relação à base da tela (`winH - winW * 0.42f`).
  - Isso liberou uma faixa horizontal de ~200px de altura totalmente vazia na parte inferior da tela, onde as setas circulares `◀` e `▶` foram posicionadas com conforto absoluto, sem nenhum risco de toque acidental no botão 7 ou no D-Pad.
- **Resolução do Problema de "Adivinhar o Ponto de Clique" (Hit-Testing por Menor Razão de Distância):**
  - Anteriormente, o teste de colisão avaliava a lista linear de botões com raio ampliado (1.30x) e retornava a primeira correspondência encontrada (`return b.key`). Caso a zona de tolerância de dois botões adjacentes se tocasse, o primeiro botão registrado na lista sempre vencia, mesmo se o dedo estivesse muito mais próximo do centro do segundo botão.
  - Implementado o algoritmo de menor razão euclidiana normalizada:
    `ratio = dist / maxRadius` (para círculos) e `max(dx/maxW, dy/maxH)` (para pílulas/retângulos).
    O botão que tiver a menor razão em relação ao ponto de toque sempre vence. Isso garante resposta tátil instantânea, sem sensação de imprecisão ou deslocamento.
- **Centralização Simétrica Vertical e Balanceamento de Colunas em Paisagem:**
  - No modo vertical, as setas `◀` e `▶` agora usam como âncora o centro exato da tela (`winW / 2`), ficando posicionadas simetricamente à esquerda (`winW/2 - spacing`) e à direita (`winW/2 + spacing`), alinhando-se com a pílula do `MAPA` e a barra de gestos do Android.
  - No modo paisagem, transferir as setas `◀` e `▶` para a coluna esquerda (entre o `MENU` e o D-Pad) equilibrou perfeitamente as duas metades da tela. O polegar esquerdo agora pode facilmente alterar poções/itens enquanto navega, e o polegar direito ganha espaço desobstruído para os 5 botões de ação e os botões de sistema `MAPA` e `R`.
- **Botão de Alternância para Ocultar/Reexibir Controles Virtuais (Eye Toggle Overlay):**
  - Implementado um botão sutil de olho no canto inferior esquerdo da tela (`btn_eye_open` e `btn_eye_closed`), renderizado com estética glassmorphic e 4x supersampling.
  - Permite ao jogador desativar completamente a sobreposição dos botões na tela com um único toque, deixando a imagem do jogo 100% desobstruída (ideal para cutscenes, apreciação de cenários ou quando estiver usando um gamepad Bluetooth físico).
  - Para reexibir, basta tocar no ícone discreto do olho fechado.
  - Prevenção de reativação indesejada: toques comuns na área do jogo não reexibem os controles, apenas o botão de alternância dedicado.
  - Ao ocultar com teclas pressionadas, o sistema limpa `s_activeFingers` e emite `keyReleased` para o VM J2ME, evitando que o personagem continue andando sozinho.
  - Acompanhado de leve vibração tátil (haptic feedback) de confirmação.
## Sessão 9 (Port Homebrew para Nintendo Switch — libnx, RomFS, Makefile e CI)
- **Ciclo de Vida do Switch OS e `appletMainLoop()`:**
  - Aplicações Homebrew no Nintendo Switch precisam respeitar o loop do applet manager (`appletMainLoop()`). Conectar essa verificação ao `Platform::shouldQuit()` permite que o jogo responda de forma nativa e instantânea ao botão HOME, transição para modo de suspensão/sleep e saída graciosa para o hbmenu sem travar o console.
- **Armazenamento Seguro: RomFS (Somente Leitura) vs. SD Card (Escrita RMS):**
  - O formato `.nro` permite embutir todos os dados do jogo via `elf2nro --romfsdir`. No boot, a chamada `romfsInit()` monta os arquivos internamente no ponto de montagem `romfs:/`.
  - Como o RomFS é estático e somente leitura, os saves e dados de configuração (RMS) devem obrigatoriamente ser direcionados para o cartão de memória (`sdmc:/switch/heroes_lore/`). O método `Platform::getStorageDir()` cria automaticamente esses diretórios via `mkdir(path, 0777)`.
- **Tratamento de Exceções C++ no Toolchain devkitA64:**
  - O template padrão do devkitPro para Switch muitas vezes inclui a flag `-fno-exceptions`. No entanto, a máquina virtual de bytecode J2ME do nosso projeto utiliza `throw JavaThrow{ex}` para propagar exceções da linguagem Java entre os frames de execução do interpretador (`interp.cpp`). Portanto, o `Makefile.switch` deve ser configurado com `-fexceptions` explícito.
- **Empacotamento All-in-One via RomFS:**
  - Com o script `tools/prepare_switch_romfs.py`, unificamos `reference/extracted` (bytecodes `.class`, mapas `.dat`, trilhas sonoras `.mid`, fontes e gráficos) e `assets` em uma pasta temporária `romfs/`.
  - O arquivo final `heroes_lore.nro` fica com ~5 MB completamente autocontido, dispensando a necessidade de copiar pastas avulsas para o SD Card. Basta copiar o `.nro` e jogar.
- **CI / GitHub Actions com Container Oficial `devkita64`:**
  - Configurado workflow automatizado em `.github/workflows/build-switch.yml` usando o container Docker oficial `devkitpro/devkita64:latest`. A cada push, as dependências e ferramentas já integradas na imagem oficial compilam o executável `.nro`, que é verificado por `tools/inspect_nro.py` e disponibilizado nos artefatos.

## Sessão 10 (Diagnóstico do Boot no Switch e Correção de CI / RomFS)
- **Eliminação do Erro 403 no devkitPro Pacman:**
  - O repositório `pkg.devkitpro.org` possui proteção Cloudflare que rejeita sincronizações (`-Sy` / `-Syu`) originadas dos IPs do GitHub Actions com HTTP 403.
  - Como o container `devkitpro/devkita64:latest` já inclui os headers do SDL2 e as bibliotecas estáticas (`libSDL2.a`, `libSDL2_mixer.a`, etc.) pré-instaladas em `/opt/devkitpro/portlibs/switch`, a invocação do `dkp-pacman` é totalmente desnecessária, eliminando os erros do pipeline de CI.
- **Causa do Fechamento Imediato no Boot (`SDL_INIT_HAPTIC`):**
  - O backend SDL2 para Nintendo Switch (libnx) não suporta a API legada de feedback háptico (`SDL_HAPTIC`). Chamar `SDL_Init` incluindo `SDL_INIT_HAPTIC` faz com que o `SDL_Init` falhe com código `< 0`, abortando a inicialização no primeiro milissegundo.
  - O rumble no Switch é realizado exclusivamente através da API moderna `SDL_GameControllerRumble()`. A remoção da flag restaura a inicialização perfeita do SDL2.
- **Criação de Superfície de Janela (`SDL_CreateWindow`):**
  - No Switch, o uso da flag `SDL_WINDOW_FULLSCREEN` em alguns emuladores mobile (Eden, Citron) causa tentativa de mudança de modo de exibição de desktop que pode encerrar a aplicação. A utilização da flag neutra `0` (conforme exemplos oficiais do libnx) cria a superfície direta em 1280x720 sem efeitos colaterais.
- **Espelhamento de Estrutura de Pastas na RomFS:**
  - O utilitário `tools/prepare_switch_romfs.py` foi atualizado com `dirs_exist_ok=True` para empacotar os arquivos na raiz de `romfs:/`, em `romfs:/reference/extracted/` e em `romfs:/extracted/`, garantindo compatibilidade total independente do formato com que a VM ou as classes Java solicitarem os arquivos (com ou sem barra inicial, prefixadas ou relativas).

## Sessão 11 (Estabilidade no Switch Real, Diagnóstico de Memória e Threading)
- **Eliminação do Crash / Kernel Panic do Atmosphère (`2168-0001` no `hbloader`):**
  - O utilitário `nacptool` estava sendo invocado com a flag fixa `--titleid=0100686C73000000`. No Atmosphère e no Horizon OS, especificar um Title ID arbitrário em binários `.nro` faz com que o loader do sistema tente vincular o aplicativo a um TID inexistente no console, gerando um kernel panic fatal que forçava o reboot do Switch.
  - A remoção da flag `--titleid` no `Makefile.switch` permite que o `hbloader` execute o Homebrew no espaço de memória limpo de homebrew padrão (`010000000000100d`), cessando completamente os crashes do sistema.
- **Diagnóstico Crítico: Applet Mode (Álbum) vs. Title Override:**
  - O crash dump gerado no console físico revelou que o aplicativo estava sendo iniciado via Applet Mode (clicar no Álbum). No modo Applet, o Horizon OS reserva apenas ~32 MB a 40 MB de memória RAM total para o processo.
  - O carregamento das centenas de classes J2ME, buffers de tela e assets estoura rapidamente essa cota.
  - No Switch real, homebrews com engines ou máquinas virtuais devem ser executados via **Title Override** (segurar o botão <kbd>R</kbd> ao abrir qualquer jogo comercial ou demo instalado no console), garantindo acesso a todos os ~3.5 GB de memória RAM disponíveis.
- **Suporte a Threads (`std::thread`) no Toolchain devkitA64:**
  - No GCC `aarch64-none-elf`, chamadas a `std::thread` requerem explicitamente a flag `-pthread` no compilador e no linker. A ausência da flag impedia o funcionamento da thread principal de lógica do jogo iniciada por `GameMIDlet.startApp()` (`bs.var_bs_a.c()`).
- **Persistência de Logs em Disco com `dup2`:**
  - Adicionado redirecionamento no boot de `STDOUT_FILENO` e `STDERR_FILENO` diretamente para o descritor de arquivo de `sdmc:/heroes_lore_boot.log` usando `dup2()`.
  - A função de log `hl::boot_log` foi unificada no namespace `hl` em `src/platform/platform.h` e conectada a `VM::fatal()`, garantindo que qualquer encerramento inesperado ou exceção não tratada seja imediatamente persistida no cartão microSD.

## Sessão 12 (Substituição de std::thread por SDL_CreateThread e Remoção de thread_local)
- **Incompatibilidade Crítica de `std::thread` no devkitA64 / libnx:**
  - O GCC 15 `aarch64-none-elf` no ambiente bare-metal do devkitPro não implementa um runtime completo de POSIX pthreads. Chamar `std::thread` faz com que o construtor invoque `std::terminate()` / `abort()`, encerrando imediatamente o processo do Switch com a mensagem "O software foi fechado pois ocorreu um erro".
  - A solução ideal foi migrar a criação de threads secundárias da VM J2ME (`VM::startThread`) para a API nativa do SDL2: `SDL_CreateThreadWithStackSize(javaThreadRunner, "HL_JavaThread", 2 * 1024 * 1024, args)`. O SDL2 possui suporte direto ao kernel do Switch via chamadas oficiais `threadCreate()` e `threadStart()`, com stack dedicado de 2MB.
- **Eliminação do `thread_local` no Ponteiro `tctx`:**
  - A variável `extern thread_local ThreadCtx* tctx` dependia do registrador de Thread Local Storage (`TPIDR_EL0`). Em sistemas sem loader dinâmico completo de TLS, threads criadas fora da libc não têm seus segmentos `.tbss`/`.tdata` inicializados, corrompendo a leitura do ponteiro e causando Data Abort / Panic ao tentar acessar `tctx->sp`.
  - Como a máquina virtual já é 100% serializada pelo Global Interpreter Lock (`gil.lock()`), a variável foi convertida para um ponteiro global padrão `ThreadCtx* tctx = nullptr;`. Cada thread salva e restaura `tctx` deterministicamente ao adquirir e liberar o GIL (inclusive durante `sleepMs` e `monitorEnter` com `SDL_Delay`), garantindo segurança absoluta de concorrência sem depender de TLS.

## Sessão 13 (Música MIDI via TinySoundFont, Inversão de Botões A/B e Orientação Retrato TATE no Switch)
- **Sintetizador MIDI em Tempo Real via TinySoundFont (`tsf.h` + `tml.h`):**
  - O pacote `switch-sdl2_mixer` fornecido pelo devkitPro não é compilado com backend MIDI (nem TiMidity nem FluidSynth), fazendo com que `Mix_LoadMUS_RW` retorne `nullptr` ou silencie faixas MIDI.
  - Em vez de depender de codecs dinâmicos do sistema, incluímos as bibliotecas header-only `TinySoundFont` e `TinyMidiLoader` em `third_party/`, acompanhadas do SoundFont General MIDI de alta qualidade `TimGM6mb.sf2` (~5.7 MB) em `assets/soundfont/`.
  - Criada a classe `hl::MidiSynth` que processa eventos MIDI e renderiza blocos de áudio PCM de 16 bits a 44.1 kHz estéreo diretamente no stream de reprodução do SDL2 via `Mix_HookMusic`.
  - Além disso, adicionada checagem defensiva dos magic bytes de cabeçalho MIDI (`MThd`) em `Player_realize` para identificar faixas `.mid` mesmo quando a classe J2ME não especificar o mime-type exato.
- **Alinhamento do Mapeamento Físico de Botões no Nintendo Switch:**
  - O subsistema `SDL_GameController` utiliza a convenção posicional padrão do Xbox (onde o botão de baixo é o 'A' e o da direita é o 'B'). No Nintendo Switch, os rótulos físicos são invertidos: o botão da direita é o **A** (selecionar/confirmar) e o de baixo é o **B** (cancelar/voltar).
  - Sob `#ifdef __SWITCH__`, invertemos o mapeamento para associar `SDL_CONTROLLER_BUTTON_B` (botão direito no Switch) ao código '5' (Ataque / Confirmar) e `SDL_CONTROLLER_BUTTON_A` (botão inferior no Switch) ao código '-7' (RSK / Cancelar / Status), além de `SDL_CONTROLLER_BUTTON_Y` para '1' (Skill 1) e `SDL_CONTROLLER_BUTTON_X` para '3' (Skill 2).
- **Orientação Retrato / Vertical (TATE Mode) no Switch:**
  - O console Nintendo Switch possui proporção 16:9 em tela cheia (1280x720), o que deixa colunas laterais pretas para o jogo em proporção clássica 240x320.
  - Foi implementado um alternador de orientação de tela (`s_screenRotation`):
    - Modo 0: Paisagem horizontal padrão (1280x720).
    - Modo 1: Retrato / TATE 90° horário (720x1280).
    - Modo 2: Retrato / TATE 270° anti-horário (Flip Grip, 720x1280).
  - Quando ativado, o jogo e o overlay virtual são renderizados em um target texture intermediário de 720x1280 (`SDL_TEXTUREACCESS_TARGET`) e apresentados no display com `SDL_RenderCopyEx` no ângulo de rotação correspondente.
  - A função `getTouchCoords()` traduz algebricamente as coordenadas normalizadas dos dedos (`ev.tfinger.x`, `ev.tfinger.y`) para o espaço virtual rotacionado, permitindo jogar com comandos touchscreen com total naturalidade como se o Switch fosse um smartphone gigante.

## Sessão 14 (Créditos Oficiais do Port e CI/CD Multiplataforma com Releases)
- **Estrutura Interna dos Arquivos Babble / Localização (`lang.en-GB`):**
  - O arquivo de localização binário possui um inteiro de 4 bytes de tamanho no início, seguido por uma tabela de offsets relativos de `N * 4` bytes (onde $N = 3951$ strings).
  - Cada entrada de texto é precedida por 2 bytes de comprimento do bloco (`len + 2`) seguidos pela representação canônica `writeUTF` do Java (2 bytes de comprimento da string seguidos pelos bytes codificados em UTF-8).
  - Desenvolvido o script `tools/patch_credits.py` para injetar os créditos de David Kalil Braga (2026) no menu Sobre (`bl.class`) e no menu Info -> Cred (string 1237).
  - No menu Sobre, a versão original `v.0.0.2` (tradução brasileira da Open Mind Team de 2008) foi rigorosamente mantida intacta antes dos créditos do port nativo, alterando de forma cirúrgica a string concatenada no bytecode de `bl.<init>` e substituindo o bytecode subsequente por NOPs para não deslocar labels nem offsets do interpretador.
  - No menu Info -> Cred (`lang.en-GB`), o script recalcula matematicamente os offsets de todas as strings subsequentes deslocadas na tabela, preservando a integridade das 3951 strings do jogo.
- **Automação de Builds e Releases no GitHub Actions (`.github/workflows/build.yml`):**
  - Unificados os fluxos de compilação em um pipeline multi-job paralelo que testa e compila as 3 plataformas simultaneamente:
    1. **Nintendo Switch:** Container Docker oficial `devkitpro/devkita64:latest` montando RomFS e compilando `heroes_lore.nro`.
    2. **Windows x64:** Runner `windows-latest` com MSYS2 UCRT64, CMake e Ninja, empacotando o executável, DLLs e pasta `assets/` em `heroes_lore_windows_x64.zip`.
    3. **Linux x86_64 (AppImage / Steam Deck):** Runner `ubuntu-22.04` montando `AppDir/` com `dist_linux/` (AppRun, ícone, `.desktop`) e empacotando via `appimagetool` em `heroes_lore_linux_x86_64.AppImage`.
    4. **Android:** Runner `ubuntu-latest` com Java 17 e NDK 26, gerando os APKs Release assinados (`universal` e `arm64-v8a`).
  - Integrado o step de release via `softprops/action-gh-release@v2`, que automaticamente coleta os binários das 4 plataformas e cria a release oficial no GitHub sempre que uma tag de versão (`v*`) for enviada ou acionada via `workflow_dispatch`.

## Sessão 15 (Aspect Ratio, Molduras Temáticas / Bezels e Taxa de Quadros 30 vs 60 FPS)
- **Geração de Bezels Temáticos Procedurais em Alta Resolução (1920x1080):**
  - Jogos J2ME clássicos em resolução 240x320 possuem proporção vertical 3:4. Em monitores modernos de computador (16:9 / 16:10), Nintendo Switch e Steam Deck, a renderização centralizada deixa grandes barras pretas laterais (pillarbox).
  - Desenvolvido o gerador `tools/generate_bezel_textures.py` que gera imagens widescreen 1920x1080 em formato binário raw `.rgba` e `.png`:
    - `bezel_soltia`: Pilares de ardósia mística, medalhão heráldico da espada alada com halo azul-arcano, runas antigas luminosas, placa comemorativa entalhada em bronze e ouro com os créditos oficiais para David Kalil Braga (2026), e vinhetas com drop shadow e chanfros dourados para integração suave com a área 3:4 do jogo.
    - `bezel_slate`: Versão minimalista em ardósia vulcânica e aço escovado com micro-chanfros sóbrios para jogadores que preferem discrição.
    - `bezel_black`: Modo clássico para puristas de barras pretas.
- **Eliminação de Glifos Não Mapeados (Tofu/Quadradinhos Unicode) via Desenho Geométrico:**
  - Caracteres Unicode rúnicos nórdicos muitas vezes não estão implementados nas fontes TTF padrão do Windows/Linux/Android, resultando no símbolo de substituição quadrado (`□`).
  - A solução foi implementar o traçado procedural geométrico das runas (Algiz, Tiwaz, Fehu, Sowilo, Gebo, Othala) diretamente via linhas e coordenadas matemáticas em Python, desenhadas como sulcos rúnicos com núcleo branco brilhante e brilho translúcido em cyan neon. Isso elimina qualquer dependência de fontes externas e garante renderização 100% perfeita em qualquer SO.
- **Folha de Fonte Bitmap Dedicada para OSD e Isolamento de Célula:**
  - Criada a textura `assets/ui/font_osd.rgba` (352x216) com os 96 caracteres ASCII (32 a 127) em células de 22x36 pixels.
  - Para evitar vazamento vertical de sombras (bleed) entre linhas adjacentes, cada caractere é desenhado em uma imagem intermediária isolada (`cell_img = Image.new('RGBA', (cell_w, cell_h))`) e colado na grade com `paste()`.
  - O renderizador nativo em C++ mapeia cada caractere diretamente via `SDL_RenderCopy` com escala suave, produzindo um HUD flutuante moderno em vidro fosco no topo da tela, com fade out automático após 2.5 segundos.
- **Frame Pacer de Alta Precisão (30 FPS Nostalgia vs 60 FPS Fluidez Máxima):**
  - Monitores modernos com refresh rates de 60Hz, 120Hz ou 144Hz podem introduzir oscilações caso dependam apenas de `SDL_Delay(8)`.
  - Implementado o método `Platform::framePacerWait()` utilizando o relógio de alta frequência do hardware (`SDL_GetPerformanceCounter()` e `SDL_GetPerformanceFrequency()`).
  - O algoritmo dorme via `SDL_Delay` pela maior parte do tempo restante do frame (deixando uma folga de 1.5ms) e realiza um spin-wait fino nos microssegundos finais via `SDL_Delay(0)`. Isso atinge precisão sub-milissegundo sem desperdício de CPU.
  - O jogador pode alternar instantaneamente entre 30 FPS Clássico (tempo de quadro travado em 33.33ms para reproduzir com fidelidade absoluta a sensação e física do celular de 2007-2008) e 60 FPS Fluido (tempo de quadro em 16.66ms para máxima fluidez).
- **Persistência Transparente em `hl_settings.ini`:**
  - Todas as opções de exibição (`bezel_mode`, `target_fps`, `aspect_mode`, `fullscreen` e tamanho redimensionado de janela `window_w`, `window_h`) são salvas e restauradas automaticamente em `hl_settings.ini` no diretório retornado por `Platform::getStorageDir()`, garantindo que o jogo reabra exatamente como o usuário configurou.

- **Descoberta do Limitador Interno Arcaico de 14 FPS em `bs.java` e Emancipação do Frame Pacer:**
  - *Diagnóstico do Sintoma:* Ao alternar entre 30 e 60 FPS, nenhuma diferença era percebida visualmente.
  - *Investigação do Bytecode:* Em `bs.java` (linhas 79-84), o método `b()` chama `a(this.var_long_a, this.var_int_d)`. Dentro dele, `l4 = System.currentTimeMillis() - var_long_a; if (l4 < var_int_d) Thread.sleep(var_int_d - l4)`. O valor `var_int_d` é inicializado como `1000 / var_int_c`, sendo que `var_int_c` vem da matriz original de taxas do celular: `var_int_arr_a = {8, 10, 14, 18}`. Por padrão, o jogo roda no índice 2 (`14 FPS`), o que força um `Thread.sleep` de ~71ms a cada quadro!
  - Como o `paint()` da VM já consumia 71ms bloqueado no sleep Java, quando o controle retornava ao `main.cpp` e chamava `Platform::framePacerWait()`, o tempo decorrido já era superior tanto a 33.3ms (30 FPS) quanto a 16.6ms (60 FPS). Portanto, em ambos os modos, o jogo estava travado em ~14 FPS pelo próprio bytecode!
  - *Solução Elegante:* Em `src/vm/natives.cpp` (`Thread_sleep`), identificamos sleeps de baixa duração (<= 80ms) durante a renderização do display e desviamos para `vm.sleepMs(0)`. Isso transfere 100% do controle de cadência para o `Platform::framePacerWait()` nativo em C++, com precisão de microssegundos via `SDL_GetPerformanceCounter()`. Agora o ciclo de opções oferece:
    - **30 FPS (Modo Fluido Padrão / 33.3ms):** Resposta imediata, cadência equilibrada.
    - **60 FPS (Modo Turbo / 16.6ms):** Fluidez máxima, 2x mais rápido para grind e exploração ágil.
    - **15 FPS (Nostalgia J2ME 2007 / 66.6ms):** Reprodução autêntica da cadência lenta original do celular.

- **Implementação do True Widescreen (Expansão Real de Viewport 568x320):**
  - O usuário solicitou que o modo widescreen não esticasse os pixels (distorção anamórfica rejeitada), mas expandisse a visão lateral do mapa, revelando mais terreno sem deformação.
  - *Engenharia da Resolução:* A altura nativa do jogo é 320p. A proporção 16:9 exata para altura 320 é $320 \times \frac{16}{9} \approx 568.88 \rightarrow 568$ pixels de largura.
  - Em 568x320, o motor de mapa (`ae.java`) calcula dinamicamente $n8 = (568 - camX - 1) / 16$, passando a renderizar **35 colunas de tiles de 16x16** simultaneamente na tela, em comparação com as 15 colunas do modo 240p original!
  - No modo True Widescreen, todos os pixels são perfeitamente quadrados (proporção 1:1, pixel-perfect). O jogador enxerga mais do cenário, monstros e caminhos nas duas extremidades laterais da tela.
  - Em mapas de interiores pequenos (ex: casas com 17 colunas = 272px), o motor original em `ae.java` (linhas 547-556) já possui suporte embutido de centralização automática com preenchimento lateral limpo: `n2 = (n4 - this.var_int_c) / 2`.
  - A sincronização em tempo real ao alternar via <kbd>F7</kbd> ou <kbd>F10</kbd> atualiza as variáveis estáticas e instâncias da VM (`r.g`, `r.i`, `r.h`, `r.j`, `as.var_int_a`, `as.b`, `as.c`, `as.var_int_d`, `as.n`, `as.o`, `as.p`, `var_boolean_e`, `f`, `g`, `h`), recriando a textura SDL sem engasgos.
  - No HUD inferior de `as.java`, a quantidade de segmentos de borda de pedra se adapta via $n = (r.g - 74) / 6$ (desenhando 82 segmentos em 568px em vez de 27), enquanto poções e itens rápidos ficam perfeitamente ancorados no canto inferior direito (`r.g - 26`), e barras de HP/MP/EXP preenchem a largura total com elegância.

- **Correção Crítica: Nomes de Campos CFR vs. Bytecode Real e Indexação de `fieldMap`:**
  - *Diagnóstico do Bug da Faixa Preta:* Ao ativar o True Widescreen (568x320), a textura era redimensionada para 568x320, mas o jogo desenhava apenas os primeiros 240 pixels na esquerda e deixava o restante preto, deslocando a tela.
  - *Causa Raiz:* Duas falhas silenciosas na função `setVmStaticInt`:
    1. **Chave de `fieldMap` com Descritor de Tipo:** Na classe JVM (`ClassInfo::fieldMap`), a tabela de campos é indexada como `"nome:descritor"` (ex: `"g:I"` e `"a:I"`), e não pelo nome puro `"g"`. A busca direta por `"g"` retornava `end()`.
    2. **Divergência entre CFR e Bytecode Original:** O decompiler CFR renomeou campos duplicados na descompilação (ex: chamou o campo estático `as.a` de `as.var_int_a`, `as.d` de `as.var_int_d` e o booleano de instância `as.e` de `var_boolean_e`). No bytecode binário real dos arquivos `.class` executados pela VM nativa, o nome original é literalmente `a`, `d` e `e`!
    3. Como resultado, nenhuma variável estática em Java era atualizada; `r.g` e `as.a` continuavam travados em 240 pixels. O motor de mapa do jogo aplicava `setClip(0, 0, 240, 299)` e só renderizava 240 pixels de largura para o buffer, deixando as colunas 240 a 567 vazias.
  - *Solução Definitiva:*
    - `setVmStaticInt` e `setVmInstanceBool` foram refatorados para resolver nomes candidatos automaticamente (tanto a forma descompilada `var_int_a` quanto a forma pura de bytecode `a`), testando com sufixo de descritor (`:I`, `:Z`) e iterando os campos da classe.
    - Sincronização em tempo real de `r.g` (568), `r.i` (284), `as.a` (568), `as.b` (299), `as.c` (276), `as.d` (149), `as.n` (82), `as.o` (501), `as.p` (562).
    - Câmera reposicionada suavemente no centro pelo delta matemático (`deltaC = new_as_c - old_as_c; n.a += deltaC; n.c += deltaC;`) sem chamadas de bytecode externas ao loop de execução.
    - Agora o mapa de tiles preenche os 568 pixels por completo (35 colunas de tiles) sem nenhuma barra preta e sem distorcer sprites.

- **Eliminação do Crash ao Alternar F7 em Tempo de Execução:**
  - *Sintoma:* Pressionar <kbd>F7</kbd> durante o jogo fechava o aplicativo imediatamente, mas ao reabrir o jogo ele iniciava na nova resolução já com as configurações salvas.
  - *Causa Raiz:* O processamento de eventos do SDL (`Platform::pollEvents`) ocorre no início do laço de quadros de `main.cpp`, antes da ativação do contexto da thread principal (`tctx = &mainCtx`). Na versão inicial, tentávamos executar o método bytecode `n.g:()V` via `vm.invoke` dentro de `updateJavaViewportVariables`. Como `tctx` era `nullptr`, a checagem de sanidade da VM em `interp.cpp` disparava `fatal("invoke chamado sem ThreadCtx ativo no método: n.g")`, que executava `exit(2)`!
  - *Solução Definitiva:*
    1. Remoção de qualquer chamada a `vm.invoke` durante a captura de eventos.
    2. O ajuste das coordenadas da câmera (`n.a` e `n.c`) é realizado matematicamente pelo delta de deslocamento do centro (`deltaC = new_as_c - old_as_c`). No quadro de renderização imediatamente posterior, o próprio método `as.paint()` (já rodando sob o contexto de thread ativo `mainCtx`) executa `n.g()` nativamente e recalcula as posições relativas com 100% de exatidão.
    3. Escrita atômica de inteiros (`statics[index].i = val`) e atualização limpa sem invocar `gilLock()` reentrante (o mutex da GIL não é recursivo e causava deadlock se já estivesse retido pela thread).
    4. A alternância entre **3:4 Original com Molduras** e **16:9 True Widescreen** agora ocorre em tempo real, instantaneamente, sem travamentos e com 100% de estabilidade.
- **Arquitetura de Combos de Gamepad e Mapeamento Conflit-Free:**
  - *Desafio:* O botão <kbd>SELECT</kbd> (`BACK`) é vital para abrir e fechar o Minimapa (`tecla 0`). Se ele fosse mapeado para alternar modos com pressionamento simples, ou se disparasse a tecla `0` no `DOWN`, qualquer combo (<kbd>SELECT + START</kbd> ou <kbd>SELECT + R3</kbd>) abriria o minimapa involuntariamente antes de executar o combo.
  - *Solução Elegante com Máquina de Estados de Teclas:*
    1. No evento `SDL_CONTROLLERBUTTONDOWN` de `SDL_CONTROLLER_BUTTON_BACK`, marcamos `s_selectButtonHeld = true; s_selectUsedInCombo = false;` e **não enviamos a tecla para o jogo**.
    2. Se outro botão configurado for pressionado enquanto `s_selectButtonHeld` estiver ativo:
       - <kbd>START</kbd>: executa `Platform::toggleAspect(&vm)` (True Widescreen <-> 3:4 Original) e seta `s_selectUsedInCombo = true; s_startUsedInCombo = true;`. O jogo não abre o menu.
       - <kbd>R3</kbd> (`RIGHTSTICK`): executa `Platform::toggleBezel()` (Soltia <-> Ardósia <-> Preto) e seta `s_selectUsedInCombo = true;`.
    3. No clique isolado de <kbd>R3</kbd> (sem Select): executa `Platform::toggleFps()` (15 FPS Padrão <-> 30 FPS Turbo).
    4. No evento `SDL_CONTROLLERBUTTONUP` de `SDL_CONTROLLER_BUTTON_BACK`:
       - `s_selectButtonHeld = false;`
       - Se `!s_selectUsedInCombo`: significa que o jogador deu um toque rápido e isolado no Select para consultar o Minimapa. Enviamos atomicamente `keyPressed(48)` e `keyReleased(48)`.
       - Se `s_selectUsedInCombo`: o botão fez parte de um combo, e nenhum evento do minimapa é disparado!
    5. O clique no analógico esquerdo (<kbd>L3</kbd>) permanece mapeado diretamente para o Minimapa (`tecla 0`), oferecendo acesso imediato com uma só mão.

- **Botões Virtuais Touchscreen Adaptativos (Android e Switch):**
  - Desenvolvidas novas texturas em alta resolução com supersampling 4x via `tools/generate_gamepad_textures.py`:
    - `btn_aspect` (96x96): Moldura de monitor 16:9 em cyan neon com texto "16:9".
    - `btn_fps` (96x96): Mostrador de velocímetro esportivo em tom âmbar neon com texto "FPS".
  - Posicionamento ergonômico no canto inferior direito da tela (espelhando a barra utilitária esquerda do botão de olho), tanto no modo Retrato quanto no modo Paisagem. Esse arranjo elimina poluição visual na mão esquerda e impede toques acidentais durante o combate ou navegação com direcionais.
  - Em modo Paisagem (Landscape), o cálculo de layout (`calculateLayout`) foi atualizado para reconhecer a largura expandida do True Widescreen (`568x320`), impedindo o colapso das margens virtuais e garantindo que os botões fiquem confortavelmente acessíveis nas extremidades laterais sem obstruir a ação.
  - Os ícones utilitários permanecem visíveis mesmo com o gamepad translúcido desativado, permitindo restaurar os controles ou alterar gráficos/velocidade a qualquer momento com um simples toque na tela.

- **Diagnóstico e Correção de Deslocamento de Menus Modais no Aspect Ratio:**
  - *Problema:* Se o jogador alternasse a proporção (ex: 16:9 True Widescreen <-> 3:4 Original) com uma tela modal aberta (como o menu de Status/Itens), a janela do menu aparecia cortada e deslocada para a direita, normalizando apenas ao fechar e reabrir.
  - *Causa Raiz:* Classes modais baseadas em `cb` (`ai`: Status/Itens/Equipamentos, `bp`: Loja, `bf`: Baú, `ax`: Refino, `aa`: Forja) calculam suas coordenadas X e Y apenas no construtor estático (`var_int_a = r.i - 100`, `b = r.j - 122`) com base no centro da tela `r.i`. Ao alternar a resolução, `r.i` mudava (de 120 para 284 ou vice-versa), mas as variáveis estáticas dos menus permaneciam com os valores da resolução anterior. Além disso, a hierarquia de janelas `cb` usa flags de dirty repainting (`var_boolean_a` e `var_boolean_b`) que impediam o redesenho imediato das coordenadas atualizadas.
  - *Solução Arquitetural:*
    1. Em `updateJavaViewportVariables()`, todas as classes modais têm seus campos estáticos de coordenadas (`a:I` e `b:I`) recalculados para o novo centro `(g_screenWidth / 2) - 100` e `(g_screenHeight / 2) - 122`.
    2. Foi implementado o varredor recursivo `invalidateCbHierarchy()`, que localiza o objeto ativo singleton (`ai.a`, `bp.a`, etc.) e percorre em cadeia todas as subjanelas/abas filhas (`b:Lcb;`), setando suas flags booleanas de sujeira (`a:Z` e `b:Z` em `cb`) como `true`.
    3. Com isso, os menus se reposicionam instantaneamente no centro exato da tela ao alternar o aspect ratio, com 0% de deslocamento ou artefato visual.

- **Resolução do Conflito de Pacotes no APK Android (`INSTALL_FAILED_UPDATE_INCOMPATIBLE`):**
  - *Causa Raiz:* O `android/app/build.gradle` utilizava `signingConfig signingConfigs.debug`. Em ambientes de CI efêmeros como o GitHub Actions (`ubuntu-latest`), a cada nova execução um runner limpo é instanciado. O Gradle gerava um arquivo `debug.keystore` novo a cada build, produzindo certificados com chaves RSA e impressões digitais SHA-1 distintas. O Android Package Manager bloqueia a instalação de APKs com assinaturas diferentes sobre o mesmo `packageId` por segurança, disparando erro de "conflito de pacote" e forçando o usuário a desinstalar o app prévio.
  - *Solução Definitiva:* Criado um keystore dedicado de release permanente (`android/app/heroes_lore.keystore`, com validade de 10.000 dias) versionado no repositório e configurado explicitamente em `signingConfigs.release`. A partir deste commit, todos os APKs compilados no GitHub Actions compartilharão a mesma assinatura estática, permitindo atualizações sucessivas sem desinstalação e preservando os saves locais.

- **Escalonamento Responsivo e Legibilidade do Banner OSD:**
  - *Problema:* As mensagens OSD eram renderizadas com tamanho fixo (`charH = 21px`), tornando-se minúsculas (~1.3 mm) em smartphones com telas de alta densidade de pixels (1080p, 1440p) e difíceis de ler no Switch. Além disso, em modo Retrato vertical no Switch (TATE), o OSD não era desenhado no target rotacionado.
  - *Solução Arquitetural:*
    1. **Textura de Fonte em Alta Definição (704x432):** Atualizado `tools/generate_bezel_textures.py` para gerar `font_osd.rgba` com células de 44x72 pixels (2x a resolução anterior) com antialiasing supersample e sombras ricas.
    2. **Escalonamento Baseado na Menor Dimensão (`minDim`):** A altura do caractere é calculada como `34px * (minDim / 720.0f)`, resultando em ~34px no Switch (720p, +62% maior) e ~51px a ~68px em smartphones 1080p/1440p (+142% maior).
    3. **Ajuste Dinâmico de Largura:** Em modo Retrato (Portrait), se o comprimento da mensagem ultrapassar 90% da largura da tela, a fonte reduz proporcionalmente de forma fluida para que o texto nunca seja cortado nas bordas.
    4. **Margem Segura Vertical:** `bannerY` posicionado a 6.5% da altura em modo retrato para não colidir com o entalhe da câmera frontal (notch/punch-hole) do celular.
    5. **Renderização Rotacionada no Switch:** Adicionada a chamada `drawOsd(720, 1280)` no framebuffer rotacionado `s_rotateTarget`, garantindo que o OSD apareça nítido e na orientação correta mesmo ao jogar com o console na vertical (TATE 90° e Flip Grip 270°).
    6. **Suporte a Múltiplas Linhas (`\n`) e Quebra Inteligente de Palavras:** O banner agora divide textos por quebra explícita (`\n`) ou quebra automática por palavras (`word-wrap`), centralizando cada linha de forma independente no banner. Isso impede que notificações informativas sofram encolhimento de fonte para caber em uma única linha.
    7. **Mensagens Iniciais Concisas e Otimizadas por Plataforma:** A mensagem inicial foi customizada para cada ambiente (Android: `"Heroes Lore: Wind of Soltia\nPort Nativo Mobile [PT-BR]"`), eliminando referências irrelevantes a teclas de teclado de PC (`F5-F7`) em telas sensíveis ao toque e garantindo que o texto já nasça no tamanho grande e cristalino desde o primeiro frame.

- **Resolução Definitiva de Desaparecimento de Objetos de Cenário (`aj`) no Widescreen:**
  - *Sintoma:* Ao alternar para a proporção 16:9 True Widescreen (568x320), diversos objetos decorativos do cenário (camas, mesas, plantas, estantes, baús, lareiras) sumiam da tela, reaparecendo apenas ao voltar para o modo 3:4 original.
  - *Causa Raiz no Bytecode J2ME:* A classe `aj` (responsável por objetos estáticos de cenário carregados pelo mapa `ae`) pré-calculava seus limites de culling de tela em campos de instância no construtor:
    `this.b = (short)(as.a + (img.getWidth() >> 1));` (limite direito)
    `this.e = (short)(as.b + img.getHeight());` (limite inferior)
    Durante a renderização (`aj.a(Graphics, int, int)`), ela executava:
    `if (n4 < this.var_short_a || n4 > this.b || n5 < 0 || n5 > this.e) return;`
    Como o mapa era carregado originalmente em 240x320, `this.b` recebia `240 + half_w` (~256px). Quando o jogador mudava a proporção para widescreen e a câmera ou o centro da tela se expandiam para 568px, qualquer objeto cuja coordenada na tela `n4` fosse maior que 256px era sumariamente descartado pelo teste `n4 > this.b`!
  - *Solução Arquitetural Definitiva (Hook Nativo C++):*
    1. A abordagem de tentar atualizar os campos privados de memória em `vm.allObjs` era frágil: dependia de varredura prévia de memória e não cobria instâncias criadas dinamicamente ao trocar de sala ou carregar saves já em modo widescreen.
    2. A VM (`VM::findClass()`) passou a suportar vinculação automática de métodos de classes carregadas do JAR com funções nativas C++ de alta performance registradas em `natives`.
    3. Foi implementado o método nativo `aj_draw_native` para `aj.a:(Ljavax/microedition/lcdui/Graphics;II)V` em `src/vm/natives.cpp`. Ele lê as coordenadas mundiais e imagem da instância através de índices em cache e aplica culling dinâmico contra a largura e altura reais ativas da tela (`g_screenWidth`, `g_screenHeight`), chamando diretamente `g->drawImage()`.
    4. Esta solução tem complexidade $O(1)$, zero overhead de interpretação de bytecode e funciona de forma 100% perfeita em qualquer cenário e plataforma (PC, Switch e Android).

- **Harmonização e Redesign Heráldico do Painel Lateral da Moldura (`bezel_soltia`):**
  - *Problema:* Após a remoção dos atalhos de PC, as mensagens informativas ficaram empilhadas em linhas densas (`idx * 36px`) diretamente abaixo da placa do título, gerando uma sensação de layout espremido com grandes áreas vazias e desconexas nas bordas verticais.
  - *Solução Visual:*
    1. O painel direito foi redesenhado em duas estruturas nobres de pedra ardósia e ouro:
       - **Placa Superior de Título (y = 230 a 390):** Com relevo chanfrado, duplo filete em ouro polido e divisor em losango, abrigando `HEROES LORE`, `WIND OF SOLTIA` e `EDICAO DEFINITIVA RECOMPILADA`.
       - **Cartucho Heráldico Inferior (y = 430 a 870):** Caixa ampla com rebites de bronze nos cantos, filete dourado fino e divisores rúnicos com losangos facetados entre cada bloco de informação.
    2. Os textos ganharam tipografia hierárquica diferenciada (ouro para nomes principais, platina para estúdios, ciano neon para especificações de tela e áudio, prata para subtítulos).
    3. Alinhamento vertical simétrico milimétrico com o painel esquerdo: o selo inferior `— SOLTIA REBORN 2026 —` repousa em `y = 922`, perfeitamente espelhado com o carimbo sagrado `• SOLTIA •` do lado esquerdo.

## Sessão 9 (Passo 8 — Sistema de Detecção e Atualização Automática OTA via GitHub Releases)

- **Viabilidade e Arquitetura Multiplataforma para Atualizações In-App:**
  - *Windows:* Uso de `WinHTTP` (`winhttp.h` / `winhttp.lib`), dispensando qualquer DLL ou dependência externa de terceiros para conexões HTTPS com TLS moderno. O executável ativo (`heroes_lore.exe`) pode ser renomeado em execução para `.exe.old` e substituído pelo novo binário diretamente, reiniciando via `CreateProcessA`.
  - *Nintendo Switch:* A devkitPro fornece `libcurl` e `mbedtls` nas portlibs (`-lcurl -lmbedtls -lmbedx509 -lmbedcrypto`). O sistema de arquivos FAT32 do cartão SD permite renomear e substituir o arquivo ativo `sdmc:/switch/heroes_lore/heroes_lore.nro` sem bloqueio de escrita. A reinicialização para o novo executável ocorre limpamente via `envSetNextLoad("sdmc:/switch/heroes_lore/heroes_lore.nro", "")`.
  - *Linux (Steam Deck / Desktop):* Substituição in-place de executável / AppImage via `libcurl` com preservação de permissões (`chmod 0755`), reiniciando via `execv()`.
  - *Android:* No Android moderno, o sistema operacional restringe a substituição direta de bibliotecas/APKs em execução sem o instalador do sistema (`PackageInstaller`). O `Updater` baixa o APK para a pasta de arquivos (`getStorageDir()`) e abre o diálogo nativo de atualização sem perder saves através de `ACTION_VIEW` com permissão `REQUEST_INSTALL_PACKAGES`.

- **Experiência do Usuário (UX) e Proteção de Progresso (Save Game Safe):**
  - O aplicativo **nunca** força downloads automáticos nem fecha o jogo de forma abrupta.
  - Ao detectar uma nova versão (seja na inicialização em background ou via verificação manual no menu "Sobre" / tecla <kbd>F9</kbd>), o jogador é apresentado a uma caixa de diálogo nobre de Soltia (estilo ardósia com filetes dourados) que exibe claramente a versão atual e a nova versão disponível.
  - O diálogo inclui um alerta em destaque com a mensagem:
    `"ATENCAO: Salve seu progresso no jogo antes de atualizar, pois o jogo precisara reiniciar!"`
  - O usuário possui duas opções explícitas:
    - `[5 / A] Salvei e Quero Atualizar`: Inicia o download com barra gráfica percentual de progresso em tempo real e reinicializa/aplica.
    - `[7 / B] Cancelar (Salvar Primeiro)`: Fecha o modal imediatamente e devolve o controle do jogo sem baixar nada, permitindo que ele salve seu progresso com tranquilidade no menu antes de prosseguir.
  - Durante o tempo em que o modal de atualização está ativo, todos os eventos de controle são capturados com prioridade total pelo `Updater`, impedindo que o herói ou menus do jogo se movimentem por trás da janela de diálogo.

- **Integração Orgânica ao Menu do Jogo J2ME (`bl.class` / "Sobre"):**
  - A tela "Sobre" (`bl.class`) não possuía ação atribuída à tecla '5' (Ação/Ataque) no código original de 2008 (apenas a tecla RSK/'7' fechava a janela).
  - Implementado o hook nativo `bl_a_native` registrado na VM para `bl.a:(II)Z` em `src/vm/natives.cpp`. Quando o jogador pressiona '5', Enter ou o botão <kbd>A</kbd> na tela "Sobre", o aplicativo dispara a checagem assíncrona de atualização OTA com feedback imediato via OSD e modal.
  - Os créditos da tela "Sobre" foram atualizados via `tools/patch_credits.py` para exibir a dica visual intuitiva `[5 / A]: ATUALIZAR`, tornando o recurso um item oficial e visível de dentro da interface do próprio jogo.

## Sessão 10 (Correções Visuais de Widescreen 16:9 — Precedência Aritmética J2ME em `bf` e `bx`)

- **Armadilha de Precedência de Operadores em Java de 2007 (`+` vs `>>`):**
  - No código descompilado e no bytecode original de `bf.java` (Menu Principal) e `bx.java` (Submenus de INFO):
    - `bf.java`: `int n5 = n2 + (201 - k[n4].getWidth()) >> 1;`
    - `bx.java`: `bh.void_a(graphics, n2 + 201 >> 1, n3 + 9, this.b, 1);`
  - Em Java (e C/C++), o operador de adição `+` tem precedência aritmética maior que o operador de deslocamento de bits `>>`.
  - O compilador javac gerou bytecode avaliando como:
    - `(n2 + (201 - width)) >> 1`
    - `(n2 + 201) >> 1`
  - Em telas 240x320 (`n2 = 19`), a discrepância era de apenas ~10 pixels e passava despercebida nos celulares com telas minúsculas da época.
  - Em True Widescreen 16:9 (`g_screenWidth = 569`, `n2 = 184`):
    - `(184 + 201) >> 1 = 192` (em vez de `184 + 100 = 284`).
    - Como resultado, tanto a faixa/cometa vermelho de seleção do menu principal quanto o título do submenu de informações ficavam **quase 100 pixels deslocados para a esquerda**, vazando para fora do pergaminho!
- **Solução Nativa Pixel-Perfect via Hooks (`bf_draw_native` e `bx_draw_native`):**
  - Implementados os hooks nativos C++ `bf_draw_native` e `bx_draw_native` em `src/vm/natives.cpp`.
  - Calculam o centro e o deslocamento com a aritmética correta:
    - `cometX = n2 + ((scrollW - cometW) / 2) + 15`
    - `titleX = n2 + (scrollW / 2)` (ou `g_screenWidth / 2`)
  - A faixa vermelha e os títulos ficam perfeitamente centralizados e alinhados em qualquer proporção de tela (3:4 clássico, 16:9 Widescreen, 16:10, 21:9 Ultrawide).

## Sessão 11 (Isolamento de Touchscreen vs. Emulação Sintética de Mouse no SDL2)

- **Causa Raiz de Cliques Fantasmas de Ataque/Confirmação no Touch (Android e Switch):**
  - No SDL2, em plataformas móveis e consoles com touchscreen (Android e Nintendo Switch), a biblioteca por padrão sintetiza eventos de mouse (`SDL_MOUSEBUTTONDOWN` e `SDL_MOUSEBUTTONUP`) para cada toque físico na tela (`SDL_FINGERDOWN`).
  - O código de clique de mouse adicionado recentemente disparava a tecla '5' (53 / Ação / Ataque) para qualquer `SDL_MOUSEBUTTONDOWN`.
  - Como resultado, **qualquer toque no D-Pad virtual, nas setas ou na tela emitia a tecla 5 simultaneamente**, fazendo com que o herói atacasse/interagisse em vez de apenas andar suavemente.
- **Correção Definitiva e Isolamento de Plataforma:**
  1. Configuração explícita das hints do SDL2 em `Platform::init`:
     `SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");`
     `SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");`
     Desativa na raiz a geração de eventos sintéticos cruzados.
  2. Isolamento condicional por plataforma do tratador de mouse:
     Envolvido por `#if !defined(__ANDROID__) && !defined(__SWITCH__)`, garantindo que binários mobile e de console sequer compilem o código de mouse.
  3. Verificação de ID de dispositivo:
     `if (ev.button.which != SDL_TOUCH_MOUSEID)`, blindando o desktop PC contra qualquer toque sintetizado.## Sessão 12 (Pilha HTTP Nativa Android via JNI e Instalação OTA com ApkFileProvider)

- **Causa do "Atualização: Sem Conexão com a Internet" no Android:**
  - Na compilação inicial de teste do CI, as rotinas de rede no Android haviam sido temporariamente isoladas para destravar a compilação cruzada do NDK, resultando em resposta HTTP vazia (`""`).
  - Como a resposta da API do GitHub retornava vazia, o sistema ativava o mecanismo de proteção com o aviso OSD de "Sem conexão com a internet".
- **Implementação da Pilha de Rede Java Nativa via JNI (`SDLActivity.java`):**
  - Em vez de compilar e linkar a biblioteca `libcurl` com OpenSSL no NDK (o que aumentaria o APK e exigiria gerenciamento de certificados CA raiz desatualizados em dispositivos antigos), implementamos chamadas JNI diretas (`androidHttpGet` e `androidDownloadFile`) mapeadas para métodos estáticos em `org.libsdl.app.SDLActivity`.
  - As chamadas utilizam `java.net.HttpURLConnection`, que emprega a pilha de certificados TLS nativa do sistema operacional Android.
  - **Tratamento de Redirecionamentos 302 do GitHub:** Os downloads de assets no GitHub Releases redirecionam (HTTP 302 Found) para os servidores S3 da Amazon (`objects.githubusercontent.com`). O método Java implementa um loop seguro de até 5 saltos seguindo o cabeçalho `Location`.
- **Instalação Segura de APK no Android 7.0+ (Nougat ao Android 15) com `ApkFileProvider`:**
  - Desde o Android 7.0+, o sistema operacional bloqueia o compartilhamento direto de URIs `file://` com o PackageInstaller via `FileUriExposedException`.
  - Criado o `ApkFileProvider` derivado de `ContentProvider` nativo, dispensando dependências externas do AndroidX.
  - Ao concluir o download de `heroes_lore_android_universal.apk`, o arquivo é renomeado para a extensão `.apk` e aberto via `content://org.libsdl.app.provider/...` com a flag `FLAG_GRANT_READ_URI_PERMISSION`.
  - O PackageInstaller do Android abre a tela oficial de atualização do sistema para que o jogador instale a nova versão mantendo os dados e saves intactos.
- **Resolução de Crash Instantâneo no Boot (FindClass em Native Threads do Android):**
  - *Diagnóstico:* Ao iniciar o jogo no celular, a aplicação fechava subitamente no primeiro segundo ("ameaçava abrir e fechava").
  - *Causa Raiz:* No boot, `Platform::init` disparava `Updater::checkAsync(false)` em uma `std::thread` C++. Em threads nativas anexadas via `AttachCurrentThread`, a função `env->FindClass("org/libsdl/app/SDLActivity")` falha com `ClassNotFoundException` porque threads nativas utilizam o ClassLoader do sistema, que não enxerga as classes da aplicação. Sem o tratamento de `ExceptionClear()`, o runtime ART do Android abortava o processo imediatamente.
  - *Correção Definitiva:*
    1. A inicialização de JNI (`androidInitJni()`) foi transferida para `Updater::init()`, executando na thread principal do SDL onde o ClassLoader da aplicação está ativo.
    2. A referência da classe `SDLActivity` é resolvida via `SDL_AndroidGetActivity()` + `GetObjectClass` (ou `FindClass` na thread principal) e salva como `NewGlobalRef` (`s_activityClass`), permitindo seu uso seguro e instantâneo em qualquer thread de background sem novas buscas.
    3. Todas as chamadas JNI foram blindadas com `env->ExceptionCheck() / ExceptionClear()`, evitando que exceções não tratadas causem abort no ART.
    4. O `ApkFileProvider` no `AndroidManifest.xml` foi corrigido para `android:exported="false"`, atendendo às restrições estritas de segurança do Android 12+.
- **Resolução de Crash Instantâneo no Boot do Nintendo Switch (Substituição de std::thread por SDL_CreateThread):**
  - *Diagnóstico:* Ao iniciar o jogo no Nintendo Switch, o console fechava imediatamente o software antes de exibir qualquer imagem ("ameaça abrir e fecha").
  - *Causa Raiz:* Em `updater.cpp`, as funções assíncronas `checkAsync` e `startDownload` utilizavam `std::thread`. No ambiente bare-metal do devkitA64 / libnx, o runtime do GCC 15 `aarch64-none-elf` não suporta `std::thread` diretamente e dispara `std::terminate() / abort()`. Como `Platform::init` chamava `Updater::checkAsync(false)` no boot, o Switch abortava instantaneamente.
  - *Correção Definitiva:*
    1. Substituído o uso de `std::thread` pela API nativa de threads do SDL2: `SDL_CreateThreadWithStackSize(..., 1024 * 1024, ...)` e `SDL_DetachThread()`, que utiliza o suporte nativo do kernel do Switch (`threadCreate/threadStart`) via libnx com 1MB de stack dedicado.
    2. A inicialização de sockets da libnx (`socketInitializeDefault()`) foi protegida para verificar o retorno de sucesso (`R_SUCCEEDED(rc)`), evitando falhas caso o console esteja offline ou em modo avião.
    3. Removido o cabeçalho `<thread>` de `updater.cpp`.

## Sessão 13 (Aperfeiçoamento do Auto-Updater OTA Multiplataforma e Nova Identidade Visual)

- **Correção de Cancelamento e Hit-Testing nos Botões do Modal:**
  - *Diagnóstico:* Clicar em "Cancelar / Salvar Primeiro" era ignorado e disparava o download forçado em todas as plataformas.
  - *Causa Raiz:*
    1. No `pollEvents`, qualquer evento de toque (`SDL_FINGERDOWN`) ou clique de mouse estava forçando `key = 53` (Ação/Confirmar), sem verificar as coordenadas do clique.
    2. O botão B do gamepad gerava `key = -7`, mas `Updater::handleInput` checava apenas `key == 7`, ignorando o sinal negativo do MIDP RSK.
  - *Correção:* Implementado `Updater::handleClick(int x, int y)` com hit-testing exato nos retângulos `s_btnConfirmRect` e `s_btnCancelRect`, além de fechar o aviso caso o usuário toque fora da janela modal. Adicionado suporte total a `-7`, `7`, `ESC`, `Backspace` e `55` para cancelamento.

- **Design Visual Nobre da Janela Modal (Soltia Heritage):**
  - *Ajuste de Proporção:* Limitada a largura máxima a 450px para evitar distorção horizontal em formato letterbox no modo 16:9 widescreen.
  - *Espaçamento Natural de Caracteres:* Implementado o parâmetro `stepX` em `Platform::drawText` e `Platform::getTextWidth`, reduzindo o avanço horizontal entre glifos para sobrepor a margem transparente da fonte OSD. O texto agora é exibido como palavras contínuas e elegantes, sem letras excessivamente espaçadas.
  - *Botões Gráficos Reais:* Substituídas as linhas de texto bruto por botões retangulares chanfrados iluminados (Ciano/Ouro para Atualizar e Ardósia Carmesim para Cancelar).

- **Reinício Limpo e Encadeamento no Nintendo Switch (Eliminação do Erro de Fechamento):**
  - *Diagnóstico:* O Switch baixava a atualização, mas exibia erro da Atmosphere ao tentar reiniciar e não aplicava o arquivo.
  - *Causa Raiz:* Chamar `exit(0)` encerrava o processo abruptamente sem desinicializar os serviços da libnx, RomFS e SDL2. Além disso, o caminho de destino estava fixo em `/switch/heroes_lore/heroes_lore.nro`, enquanto muitos usuários executam diretamente de `/switch/heroes_lore.nro`.
  - *Correção:*
    1. O caminho do NRO executado é detectado dinamicamente via `argv[0]`.
    2. O novo NRO é gravado no caminho ativo e duplicado tanto em `sdmc:/switch/heroes_lore.nro` quanto em `sdmc:/switch/heroes_lore/heroes_lore.nro`.
    3. Em vez de `exit(0)`, chama-se `Platform::requestQuit()`. O loop principal de `main()` encerra normalmente, executa `Platform::shutdown()` com `romfsExit()` e retorna 0. O hbmenu então executa `envSetNextLoad` sem qualquer erro do sistema.

- **Resolução do Executável do Windows ("Incompatível com o PC"):**
  - *Causa Raiz:* O atualizador baixava o asset `heroes_lore_windows_x64.zip` e o renomeava diretamente para `heroes_lore.exe`.
  - *Correção:* Atualizado o GitHub Actions (`build.yml`) para publicar tanto o ZIP quanto o executável nativo `heroes_lore.exe` na Release. O atualizador no Windows agora baixa diretamente o binário `heroes_lore.exe` e o substitui atomicamente.

- **Resolução de Falha no Download do Linux (AppImage):**
  - *Causa Raiz:* No AppImage, o diretório de execução atual (`"."`) é uma montagem somente-leitura em squashfs, causando falha de permissão no `fopen`.
  - *Correção:* `Platform::getStorageDir()` agora utiliza o diretório de dados gravável do usuário (`~/.local/share/heroes_lore`). Ao aplicar a atualização, o caminho real do arquivo executado é lido via `getenv("APPIMAGE")`.

## Sessão 14 (Refinamento do OTA Multiplataforma: Layout Responsivo, Switch A/B e Auto-Update PC/Console)

- **Diagnóstico da Não Detecção da Versão Nova no PC (Windows):**
  - *Causa Raiz:* Na release v1.0.5 do GitHub, apenas o asset `heroes_lore_windows_x64.zip` havia sido publicado (porque o upload do artefato no CI colocava o executável sob a subpasta `dist_windows/heroes_lore.exe`, sendo ignorado pelo publicador do release na raiz). No código C++, o atualizador procurava estritamente por `"heroes_lore.exe"`. Como não encontrava um asset exato com esse nome, o parser retornava falso e informava que nenhuma versão nova estava disponível.
  - *Solução Definitiva:*
    1. **Parser Flexível com Lista de Candidatos por Prioridade:** `getCandidateAssetNames()` foi introduzido. No Windows, ele testa `heroes_lore.exe`, `heroes_lore_windows_x64.zip` e `heroes_lore.zip`. O primeiro que existir no release é selecionado.
    2. **Suporte Nativo a Atualização por ZIP no Windows:** Se o asset baixado for um `.zip`, `applyUpdate()` extrai o conteúdo no diretório da aplicação via `tar.exe` nativo do Windows 10/11 (ou PowerShell `Expand-Archive`). Se for `.exe`, realiza a substituição in-place com renomeação para `.old`.
    3. **Upload Direto no GitHub Actions:** O workflow `.github/workflows/build.yml` foi corrigido para copiar `build/heroes_lore.exe` para a raiz do artefato, garantindo que as próximas releases publiquem simultaneamente o executável solto de ~4MB e o ZIP portátil completo.

- **Resolução do Modal Microscópico em Telas de Alta Resolução / Celular em Modo Retrato:**
  - *Causa Raiz:* As dimensões do modal estavam travadas em `modalW = std::min(450, ...)` e tamanho de caractere fixo em `18px`. Em telas modernas de smartphones (1080x2400 ou 1440p em modo vertical/retrato), uma caixa de 450px representava uma fração diminuta da tela, tornando as letras ilegíveis.
  - *Solução Arquitetural Responsiva:*
    1. **Detecção de Orientação e Densidade:** Distinção automática entre Retrato (`winH > winW`) e Paisagem (`winW >= winH`).
    2. **Modo Retrato (Smartphones):** O modal agora ocupa **92% da largura da tela** (`modalW = std::clamp((int)(winW * 0.92f), 280, 1100)`) com altura proporcional (`modalH = (int)(modalW * 0.92f)`).
    3. **Modo Paisagem (Nintendo Switch 720p, Steam Deck, PC):** A janela modal foi expandida para **65% da tela** (`modalW = ~800px`, `modalH = ~520px` no Switch em vez de 450x270px), ocupando o centro da tela de forma imponente e confortável.
    4. **Tipografia Dinâmica Escalável:** A fonte OSD agora escala dinamicamente com a altura da janela (`charH = std::clamp((int)(modalH * 0.052f), 16, 32)`). No celular, isso eleva os glifos para 32px; no Switch, para 26px; e no PC, para 28px.
    5. **Botões Touch com Ampla Área de Clique:** No celular, os botões virtuais agora atingem mais de 70px de altura e 400px de largura, proporcionando toque ergonômico imediato sem esforço.

- **Inversão dos Botões A e B no Nintendo Switch (Layout Físico Nintendo):**
  - *Diagnóstico:* Na janela de atualização, ao apertar o botão A físico da Nintendo (direita) a tela fechava (cancelava); ao apertar o botão B físico (baixo) o jogo tentava atualizar.
  - *Causa Raiz:* No loop de eventos do SDL2 para o modal, o código checava diretamente `ev.cbutton.button == SDL_CONTROLLER_BUTTON_A` (que no padrão Xbox corresponde ao botão inferior = B físico da Nintendo) e `SDL_CONTROLLER_BUTTON_B` (que corresponde ao botão direito = A físico da Nintendo).
  - *Solução:* O loop de eventos do modal agora utiliza a função `mapControllerButton(ev.cbutton.button)`. No Switch (`#ifdef __SWITCH__`), o botão físico A (direita) gera o código `53` (Confirmar/Atualizar), e o botão físico B (baixo) gera `-7` (Cancelar/Fechar).
  - Além disso, os textos de atalhos exibidos nos botões agora mostram claramente:
    - No Switch: `[ BOTAO A ]` para atualizar, `[ BOTAO B ]` para cancelar.
    - No Celular: `[ TOQUE ]` para atualizar e fechar.
    - No PC/Linux: `[ A / ENTER ]` para atualizar, `[ B / ESC ]` para cancelar.

- **Substituição Definitiva do NRO no Cartão SD do Nintendo Switch:**
  - *Diagnóstico:* O Switch baixava o arquivo NRO (77.9 MB), exibia "Reinicie para continuar", mas ao fechar e reabrir o jogo continuava na versão anterior.
  - *Causa Raiz:*
    1. `Platform::getExecutablePath()` recebia `argv[0]` relativo (ex: `"heroes_lore.nro"`), sem o prefixo de montagem do cartão SD (`sdmc:/`).
    2. Como o processo em execução não possuía o caminho canônico, a cópia para `"heroes_lore.nro"` falhava silenciosamente ou era gravada fora do diretório do Homebrew Menu.
    3. `runDownloadThread` não checava o retorno booleano de `applyUpdate()`. Mesmo se a substituição falhasse, o estado era marcado como `DOWNLOAD_COMPLETE` e exibia falsamente "Arquivo instalado com sucesso!".
  - *Solução Robusta:*
    1. **Resolução Canônica de Caminho (`resolveSwitchNroPath`):** O caminho real é validado sequencialmente contra o `execPath` com prefixo `sdmc:/`, os diretórios padrão do console (`sdmc:/switch/heroes_lore/heroes_lore.nro`, `sdmc:/switch/heroes_lore.nro`) e normalizado.
    2. **Validação de Tamanho do Download:** O arquivo `.download` é verificado antes da substituição para garantir que tem tamanho íntegro de NRO (> 1 MB).
    3. **Substituição Atômica com Backup:** O NRO existente é temporariamente renomeado para `.old`. O novo arquivo é copiado via stream binário em blocos de 64KB. Em caso de falha, o backup `.old` é restaurado automaticamente.
    4. **Logs Detalhados:** Toda a operação é registrada com `boot_log` em `sdmc:/heroes_lore_boot.log`.
## Sessão 15 (Atualização Integral de Assets no Windows e Blindagem do SDMC no Switch)

- **Atualização da Pasta `assets/` no Windows via Pacote ZIP Completo:**
  - *Diagnóstico:* Ao atualizar no PC, o jogo reiniciava informando no banner superior que estava na nova versão (`v1.0.7`), mas o pergaminho "Sobre" continuava exibindo a versão anterior (`v1.0.6`).
  - *Causa Raiz:* O texto da tela "Sobre" é carregado do bytecode em `assets/bl.class`. O atualizador havia baixado apenas o binário `heroes_lore.exe`, deixando a pasta `assets/` intocada com os dados antigos.
  - *Solução:*
    1. A lista de prioridade `getCandidateAssetNames()` no Windows passou a priorizar `heroes_lore_windows_x64.zip` em primeiro lugar.
    2. Antes de descompactar o ZIP, `applyUpdate()` renomeia o executável ativo `heroes_lore.exe` para `.old`, contornando a restrição de compartilhamento do Windows (`ERROR_SHARING_VIOLATION`) e permitindo que o `tar.exe` / `Expand-Archive` extraia o novo executável, DLLs e toda a árvore de `assets/` sem bloqueio.

- **Blindagem do Caminho Canônico `sdmc:/` e Gravação no Switch:**
  - *Diagnóstico:* Ao concluir o download de ~81MB no Switch, o modal exibia "Falha ao gravar arquivo de atualizacao".
  - *Causa Raiz:* `resolveSwitchNroPath()` validava `fopen(execPath, "rb")` e, caso o arquivo abrisse via diretório de trabalho relativo (ex: `"heroes_lore.nro"`), retornava o nome puro sem o prefixo `sdmc:/`. No Switch, o runtime C não possui permissão de gravação em caminhos relativos sem devoptab explícito, falhando no `fopen(..., "wb")`.
  - *Solução:* Todos os caminhos candidatos em `resolveSwitchNroPath()` agora iniciam obrigatoriamente com `sdmc:/`. A gravação tenta a cópia direta e, caso falhe por lock de arquivo, utiliza rotação atômica via `.old` e `rename` com rollback de segurança.

- **Ciclo de Validação v1.0.9 e Diagnóstico de Falha ao Gravar Atualização (Sessão 16):**
  - *Sintoma:* Ao baixar a v1.0.9 no PC (pasta `teste build` com espaços), o atualizador acusava "Falha ao gravar arquivo de atualizacao". No Switch, o mesmo erro ocorria.
  - *Causa Raiz Windows:*
    1. A função `system()` invoca internamente `cmd.exe /c "tar.exe -xf "..." -C "...""`. Quando o caminho contém espaços (ex: `teste build`), o algoritmo de parsing de aspas do `cmd.exe` remove as primeiras e últimas aspas, corrompendo os argumentos do `tar.exe` com o erro `tar.exe: Error opening archive: Failed to open ' C:\Users\...'`.
    2. Além disso, o pacote `.zip` contém DLLs (`SDL2.dll`, `SDL2_mixer.dll`) que estão carregadas na memória do processo ativo do jogo. O Windows bloqueia a sobreposição direta de DLLs em execução com `ERROR_SHARING_VIOLATION`.
  - *Solução Windows:*
    1. Eliminação total do `cmd.exe /c`: a execução de `tar.exe` e `powershell.exe` agora é realizada diretamente via `CreateProcessA` (com flag `CREATE_NO_WINDOW`), recebendo a linha de comando sem manipulação de aspas.
    2. Extração para diretório isolado (`appDir/_update_extract/`), onde nenhum arquivo está bloqueado em memória.
    3. Atualização atômica: renomeia `heroes_lore.exe` para `.old`, move o novo executável, sincroniza a árvore `assets/` via `robocopy /E /MOVE` e renomeia/move quaisquer DLLs novas.
  - *Causa Raiz & Solução Switch:*
    1. O console estava rodando a build antiga (v1.0.6) que nunca havia conseguido substituir o NRO antes.
    2. Identificado também que `romfsInit()` em `platform_sdl.cpp` mantém o descritor do NRO aberto para ler assets da partição embutida. Em `applyUpdate()`, adicionada a chamada preventiva `romfsExit()` para liberar imediatamente qualquer lock de arquivo no SD antes da cópia ou rotação atômica.

- **Ciclo de Validação v1.1.1 e Homologação Oficial do Passo 8:**
  - *Resultado Confirmado pelo Usuário:* Auto-atualização concluiu com sucesso no PC Desktop e no Nintendo Switch! Ao reabrir, ambas as plataformas iniciaram atualizadas na nova versão.
  - *Diagnóstico do "Apito de Erro" no Switch ao Confirmar o Reinício:*
    - *Causa:* Em `applyUpdate()`, chamávamos `romfsExit()` para liberar o NRO no SD. Quando o jogador confirmava o diálogo, `Platform::cleanup()` chamava `romfsExit()` uma segunda vez. Na `libnx`, a desinicialização dupla do RomFS causa erro de asserção interna do sistema operacional na saída do aplicativo (gerando o apito de erro do console, apesar de a substituição já ter sido concluída com 100% de integridade).
    - *Solução Definitiva:* Implementado `Platform::cleanupRomfs()` idempotente com flag de estado `s_romfsInitialized`. O fechamento é invocado com segurança em `applyUpdate()` e a chamada no `cleanup()` normal é neutralizada sem disparar nenhum erro de sistema.

## Sessão 17 (Passo 10 — Seletor de Idiomas / Localização: PT-BR, EN, IT, ES)
- **Estrutura dos Arquivos Binários Babble (`lang.*`):**
  - Cada arquivo de localização consiste em:
    1. `uint32` de comprimento total do arquivo.
    2. Tabela de offsets relativos com sinal de `N * 4` bytes ($N = 3951$ strings alinhadas por ID).
    3. Blocos de strings: `[uint16 block_len][uint16 utf_len][UTF-8 bytes]`.
  - A VM J2ME original em `cj.java` não valida checksum, CRC, nem hash de arquivo ou limitação de tamanho além do cabeçalho de offsets, permitindo substituição e alternância transparente em tempo real.
- **Fontes Oficiais e Extração:**
  - `lang_pt.bin` (174.588 bytes): Versão em português brasileiro da comunidade Open Mind Team (2008).
  - `lang_en.bin` (174.477 bytes): Versão original em inglês extraída da retail Nokia 6280/N73.
  - `lang_it.bin` (184.901 bytes): Versão oficial em italiano extraída do release BiNPDA S60v3.
  - `lang_es.bin` (182.947 bytes): Versão completa em espanhol com todos os 3.951 textos e diálogos traduzidos.

## Sessão 18 (Diagnóstico e Resolução de Deadlock ao Alternar Idioma)
- *Sintoma:* Ao alternar o idioma no menu de opções com as setas ou via tecla F2, o jogo congelava/travava completamente. Ao reabrir o jogo, o idioma havia mudado com sucesso.
- *Causa Raiz 1 (Deadlock de Thread nas Setas do Menu):*
  - Quando o jogador navega no menu `be.class`, o hook nativo `be_a_native` é executado na thread de execução J2ME / renderização, que já detém o GIL (`vm.gil`). Tentar adquiri-lo com `vm.gilLock()` gerava deadlock imediato.
- *Solução Definitiva (Atualização Direta dos Buffers em C++ & GIL Reentrante):*
  1. **Prevenção de Deadlock:** Implementado `VM::isGilOwner()`. Se a thread atual já é proprietária do GIL (como ocorre dentro de `be_a_native`), não tenta readquirir o lock. Se for thread externa (F2 no loop SDL), adquire normalmente.
  2. **Zero Invocação de Bytecode (C++ Puro):** O buffer binário de strings em `cj.var_cj_a` (`bais->data` e `byteArr->data`) e as variáveis de UI de `bh` são atualizadas diretamente em memória C++ em menos de 1 ms sem invocar bytecode.

## Sessão 19 (Atualização a Quente de Menus Abertos e Equipamentos)
- **Diagnóstico:**
  1. **Menus Abertos:** Algumas telas de menu não atualizavam os textos no exato momento da troca com F2. Causa: `cb.java` utiliza flags de dirty repainting (`var_boolean_a` e `var_boolean_b`) e certas telas filhas (como `bt` e `s`) copiam textos para campos de instância próprios no construtor.
  2. **Equipamentos e Itens:** Itens já instanciados mantinham o nome e descrição antigos. Causa: `ad.java` (e subclasses `l`, `t`, `e`) carrega `var_char_arr_a` (nome) e `var_char_arr_b` (descrição) apenas na criação lendo `/itm/<f>`.
- **Solução:**
  1. `Platform::reloadLanguage()` varre instâncias ativas de `ad` em `vm.allObjs`, relê as tabelas em `/itm/<f>` e atualiza os arrays UTF-16 em memória instantaneamente.
  2. Invalida as flags `var_boolean_a = 1` e `var_boolean_b = 1` de todas as instâncias de `cb` para forçar repintura no frame seguinte, além de atualizar rótulos de herói/classe em `q` (`a:[C`, `b:[C`), submenus de opções/ajuda em `bt` (`a:[[C`) e títulos/descrições de missões em `s` (`a:[C`, `b:[C`).

## Sessão 20 (Correção dos Diálogos do Mapa Ativo e Tradução Completa em Espanhol)
- **Atualização Imediata de Diálogos no Mapa Ativo e Balões de Fala:**
  - *Diagnóstico:* No código Java (`n.java` e `ae.java`), o campo estático `n.f:B` (`mapId`) é apenas temporário: assim que a rotina `n.f()` termina de construir o mapa, ela executa `f = (byte)-1;`. Ao tentar recarregar os diálogos do mapa em `Platform::reloadLanguage()`, ler `n.f` retornava `-1`, tentando abrir `m/6/-1.evt` (inexistente), o que falhava silenciosamente e mantinha o array antigo `ae.c:[Ljava/lang/Object;` intocado!
  - *Solução:* `Platform::reloadLanguage()` agora extrai `mapId` diretamente de `aeInst->f[fMapByteA->index].i` (campo `a:B` de `ae`), com fallback defensivo para `n.f`. Além disso, verifica se `ah.java` possui um balão de fala ativo na tela: se tiver, localiza a linha atual via `ah.var_byte_arr_arr_b[var_int_a][1]` e atualiza `ah.var_char_arr_a` (`a:[C`) instantaneamente.
- **Tradução Completa e Autêntica do Pacote em Espanhol:**
  - *Diagnóstico:* A ferramenta inicial `tools/create_spanish_lang.py` continha apenas um dicionário manual de 49 strings de menus e termos essenciais (`es_map`). Todas as 3.708 strings restantes (incluindo todos os 1.761 diálogos de NPCs e missões) foram clonadas do pacote em inglês (`lang_en.bin`).
  - *Solução:* Desenvolvido `tools/translate_spanish_dialogues.py`, traduzindo em lote todas as 3.951 strings com sanitização para o conjunto de caracteres da fonte bitmap J2ME (`az.java` / `small.mf`, restrito a ASCII 32..126: conversão sistemática de acentos `á/é/í/ó/ú -> a/e/i/o/u`, `ñ -> n`, remoção de `¿`, `¡` e preservação de tags `|`, `$`, `[Personagem]` e `;`). Binário oficial Babble `lang_es.bin` (182.947 bytes) gerado e sincronizado em todos os diretórios do projeto.

## Sessão 21 (Passo 9: Cloud Save & Sincronização Cruzada com Google Drive)
- **OAuth 2.0 Device Authorization Flow (RFC 8628) Multiplataforma:**
  - *Desafio:* Plataformas embarcadas e consoles portáteis como o Nintendo Switch Homebrew (`libnx`) e certos ambientes no Linux e Android não possuem um navegador web interno configurável para abrir abas ou callbacks HTTP locais (`localhost:port`).
  - *Solução:* Utilizado o fluxo oficial Google OAuth 2.0 Device Flow (Tipo de cliente: *TVs and Limited Input devices*). O jogo requisita `https://oauth2.googleapis.com/device/code`, exibindo na tela uma URL amigável (`google.com/device`) e um código alfanumérico de 8 caracteres. O usuário autoriza em seu celular ou PC. Enquanto isso, o jogo faz polling no endpoint de token com intervalos de 5 segundos. Ao detectar a concessão, salva o `refresh_token` e `access_token` em `cloud_auth.json` no armazenamento persistente do usuário.
- **Isolamento de Dados via `appDataFolder` (`drive.appdata`):**
  - O aplicativo não acessa nem vasculha arquivos pessoais do Google Drive do usuário. Utiliza exclusivamente o espaço isolado e protegido `appDataFolder`, invisível na listagem geral do Drive comum, garantindo privacidade absoluta e eliminando necessidade de auditorias restritas do Google Cloud.
- **Empacotamento Atômico em JSON e Descriptografia de Resumos:**
  - Saves RMS J2ME (`_k.rms`, `_s.rms`, `_w.rms`, `_o.rms`, `_c.rms`) são lidos e empacotados em um único arquivo estruturado `heroes_lore_save.json` com conteúdo codificado em Base64.
  - Para exibir na interface quem está salvo sem carregar a partida, o C++ inspeciona o primeiro record de cada arquivo `.rms`, descriptografa os primeiros bytes com a chave XOR J2ME `{5, 11, 8, 81, 3, 20}` e extrai a classe e o nível dos heróis (`Karis Nv.25`, etc.).
- **Restauração Segura sem Descompasso de Memória da JVM:**
  - Se o usuário restaurar um save enquanto estiver em uma partida ativa, os arquivos em disco seriam sobrescritos, mas a RAM da VM permaneceria no estado anterior, gerando corrupção no próximo salvamento.
  - *Mecanismo Inteligente:* O sistema detecta se há herói ativo em mapa (`n.var_ao_a != null`). Se houver, exige confirmação prévia e invoca `bu.d()` (retorno canônico ao Main Menu do bytecode), liberando os recursos antigos e reabrindo a tela de título onde `n.p()` relê os novos arquivos de disco de forma 100% íntegra.

## Sessão 22 (Ajuste de Escopo Google Device Flow e Unificação de Diretórios RMS)
- **Erro 400 no Device Flow do Google Cloud (`invalid_scope`):**
  - *Diagnóstico:* Ao clicar em "Conectar", o jogo exibia "Erro ao conectar com Google Cloud (400)".
  - *Causa Raiz:* O Google OAuth 2.0 Device Flow (RFC 8628 para TVs & Limited Input Devices) restringe estritamente escopos que podem ser autorizados sem um navegador embutido. Ao enviar `scope=https://www.googleapis.com/auth/drive.appdata`, os servidores da Google rejeitam a requisição com `HTTP 400: {"error": "invalid_scope", "error_description": "Invalid device flow scope: https://www.googleapis.com/auth/drive.appdata"}`.
  - *Solução:* Atualizado o escopo para `https://www.googleapis.com/auth/drive.file`. Este escopo é expressamente permitido pelo Google no Device Flow (retornando HTTP 200 com `device_code` e `user_code`) e é o modelo recomendado de privacidade e segurança: o aplicativo obtém acesso apenas aos arquivos criados por ele mesmo (`heroes_lore_save.json`), sem visualizar nenhum outro documento pessoal do Google Drive do usuário. A listagem e o multipart upload foram ajustados removendo `spaces=appDataFolder` e `parents: ["appDataFolder"]`.

- **Detecção de "Nenhum save local" mesmo com Saves Existentes no PC:**
  - *Diagnóstico:* O modal de Cloud Save exibia "Save local: Nenhum save local", apesar de o jogador possuir um save ativo no jogo (Karis Nv.4).
  - *Causa Raiz:* No Windows e Linux onde `Platform::getStorageDir()` retorna `"."`, o backend de `midp.cpp` (`rmsDir`) salva os dados em `vm.dataDir + "/rms"` (por exemplo, `build/assets/rms` ou `reference/extracted/rms`). No entanto, `cloud_save.cpp` estava concatenando `Platform::getStorageDir() + "/rms"`, resultando em `./rms` (inexistente).
  - *Solução:* Unificado o cálculo de diretório RMS na função canônica `Platform::getRmsDir(VM* vm = nullptr)`. A função avalia a VM ativa, armazena em cache o diretório RMS em uso e realiza busca de fallback nos candidatos (`assets/rms`, `build/assets/rms`, `reference/extracted/rms`). Agora tanto a VM (`midp.cpp`) quanto o Cloud Save (`cloud_save.cpp`) leem, gravam e restauram os saves exatamente na mesma pasta de arquivos, exibindo corretamente `Save local: Karis Nv.4`.

## Sessão 23 (Aperfeiçoamento do Cloud Save: Nível Autêntico, Data Local, Recarregamento Seguro e 4 Idiomas)
- **Decodificação Autêntica do Nível do Herói (`decodeHeroLevel`):**
  - *Diagnóstico:* Um personagem recém-criado de Nível 1 era exibido no modal do Cloud Save como "Karis Nv 2".
  - *Causa Raiz:* No bytecode original de `n.java` (linhas 746-758), o método `bq.b` decifra os registros de save aplicando a cifra XOR com a chave cíclica `{5, 11, 8, 81, 3, 20}`. O byte no índice 0 representa a classe do herói e o índice 1 armazena o nível. Durante a decodificação de `n.p()`, o cursor da chave avança: o byte 0 consome `key[1]` (11) e o byte 1 consome `key[2]` (8). O cálculo anterior lia um offset deslocado (`rec[3] ^ 11`), que para um save de Karis Lv.1 (`9`) gerava `9 ^ 11 = 2`.
  - *Solução:* Implementado o algoritmo autêntico de `bq.b` em `decodeHeroLevel()`. Decodificando o byte de índice 1 com a chave correspondente `key[2] = 8`: `9 ^ 8 = 1`, retornando fielmente `Karis Nv.1`.
- **Timestamp do Save Local para Comparação Clara:**
  - Implementada a função `getLocalSavesDate()` utilizando `stat()` / `st_mtime` sobre os arquivos `.rms` locais (`_k.rms`, etc.).
  - A data de modificação é formatada como `YYYY.MM.DD HH:MM`.
  - O card `LOCAL` agora exibe `Data: YYYY.MM.DD HH:MM | <Plataforma> | Karis Nv.1`, no mesmo padrão visual do card `NUVEM`. O jogador compara imediatamente os horários para saber qual save é o mais recente.
- **Eliminação do Fechamento Involuntário no Restore (Thread Safety da VM):**
  - *Diagnóstico:* Ao clicar em "Restaurar", os arquivos eram baixados com sucesso, mas a aplicação fechava subitamente. Ao reabrir, o save estava restaurado.
  - *Causa Raiz:* A rotina de restauração rodava em uma `std::thread` secundária de rede e chamava `s_vm->invoke(mNp)` (ou `bu.d()`). Na arquitetura da VM, `vm->invoke` depende estritamente do contexto de thread ativa (`tctx`). Em threads secundárias criadas fora da VM, `tctx` é `nullptr`, disparando a checagem fatal do interpretador que encerrava o processo.
  - *Solução:* A thread de background apenas substitui os arquivos RMS em disco e seta a flag atômica `s_pendingVmReload = true;`. A execução da atualização de memória da VM é despachada para a **thread principal** através de `CloudSave::update(VM* vm)`, chamada periodicamente em `Platform::pollEvents` e no loop modal de `CloudSave::drawModal`. O jogo não fecha mais, exibe mensagem de sucesso imediata e atualiza os cards do modal em tempo real.
- **Localização Multilíngue Completa nos 4 Idiomas (PT, EN, IT, ES):**
  - Todas as strings da interface do Cloud Save (títulos, subtítulos, cards, botões de ação, avisos de confirmação de restore, instruções de login do Device Flow e banners OSD de status) foram mapeadas no enum `CloudStr` e na matriz de tradução `s_translations` em `cloud_save.cpp`.
  - O idioma exibido acompanha instantaneamente a configuração ativa de `Platform::getCurrentLanguageIndex()` (Português, Inglês, Italiano ou Espanhol).

## Sessão 24 (Protagonistas Canônicos Ronin/Reah/Aramor, Fuso Horário Local e Ciclo de Mensagens)
- **Identificação Canônica dos Protagonistas em Heroes Lore: Wind of Soltia:**
  - *Diagnóstico:* O modal de Cloud Save exibia o slot 1 como "Karis Nv.1". O usuário apontou que o personagem principal se chama "Ronin".
  - *Investigação do Bytecode e Assets:* No arquivo `char/hero.tdf`, os 3 primeiros IDs de strings referenciados pela tela de carregamento de saves (`a.java` / `ce.var_z_a` / string IDs 685, 686, 687) são:
    - ID 685: **Ronin** (espada/cavaleiro — slot `_k.rms`)
    - ID 686: **Reah** (refinadora/lança — slot `_s.rms`)
    - ID 687: **Aramor** (cavaleiro protetor/escudo — slot `_w.rms`)
  - Os nomes foram corrigidos para a trilogia canônica de Wind of Soltia. Além disso, backups antigos existentes no Google Drive que ainda contenham "Karis" têm sua descrição sanitizada para "Ronin" na leitura através de `sanitizeCloudSummary()`.
- **Normalização de Fuso Horário (UTC para Horário Local):**
  - *Diagnóstico:* Ao enviar o backup, a data do Google Drive exibia 3 horas à frente do save local (ex: `18:21` na nuvem vs `15:19` no local).
  - *Causa Raiz:* A API do Google Drive retorna o timestamp de modificação `modifiedTime` em formato ISO-8601 em tempo UTC (ex: `2026-10-08T18:21:00Z`). O código estava exibindo a string bruta cortada (`18:21`), enquanto o timestamp do arquivo local era formatado via `std::localtime` (fuso de Brasília UTC-3, `15:19`).
  - *Solução:* Implementada a função `formatUtcIsoToLocalDate()` que faz o parse da data UTC e converte para o horário local do jogador via `_mkgmtime` / `timegm` e `std::localtime`. Agora tanto o save na nuvem quanto o local exibem o mesmo fuso horário, permitindo comparação cronológica exata.
- **Limpeza de Estado e Ciclo de Vida de Mensagens no Modal:**
  - *Diagnóstico:* Após restaurar um save, jogar e voltar à tela de Cloud Save, a mensagem "Save restaurado com sucesso!" continuava aparecendo na tela, inclusive mantendo o idioma anterior caso o jogador tivesse mudado de língua.
  - *Causa Raiz:* `s_statusMessage` não era limpo ao abrir (`openModal`) ou fechar (`closeModal`) a janela modal, e o texto ficava armazenado como string literal no idioma antigo.
  - *Solução:* Criado o identificador dinâmico `s_statusMsgId` mapeado para o enum `CloudStr`. Mensagens de estado passam a ser resolvidas em tempo de renderização via `tr((CloudStr)s_statusMsgId)`, adaptando-se instantaneamente a qualquer troca de idioma. Além disso, `openModal()` e `closeModal()` limpam completamente mensagens residuais e redefinem o estado para neutro (`LOGGED_IN`), garantindo uma tela limpa e sem resquícios a cada abertura.

## Sessão 25 (Portabilidade Switch timegm, Estabilidade JNI Android e Escala Responsiva Mobile)
- **Portabilidade da Conversão UTC para Nintendo Switch (devkitA64 / newlib):**
  - *Diagnóstico:* A compilação do Nintendo Switch quebrou no CI do GitHub Actions com o erro: `'timegm' was not declared in this scope; did you mean 'time_t'?`.
  - *Causa Raiz:* O toolchain devkitPro / devkitA64 utiliza a biblioteca C padrão `newlib`, que não disponibiliza a extensão GNU/BSD `timegm()`, e plataformas Windows usam `_mkgmtime()`.
  - *Solução:* Implementada a função pura e autocontida `portableTimegm(year, mon, day, hour, min, sec)` baseada em aritmética de calendário gregoriano (dias desde a época Unix 1970 considerando bissextos). A função possui zero dependências de bibliotecas de sistema ou extensões não-portáveis, compilando de forma idêntica em Switch, Windows, Linux e Android.
- **Estabilidade JNI no Android e Prevenção de Crash Fatal no ART:**
  - *Diagnóstico:* Ao clicar no botão "Conectar" na versão Android, o jogo fechava abruptamente.
  - *Causa Raiz:*
    1. No Android ART, threads nativas de background utilizam o `SystemClassLoader`, o que faz `env->FindClass("org/libsdl/app/SDLActivity")` lançar `ClassNotFoundException`. Sem o `ExceptionClear()`, qualquer invocação subsequente de JNI resulta no encerramento imediato do processo pelo runtime do Android.
    2. O uso de `std::thread` nativo do C++ sem o ciclo de vida gerenciado do SDL pode disparar `fatal error: native thread exiting without detaching from JavaVM` em versões recentes do Android.
  - *Solução:*
    1. Criado `androidCloudInitJni()` que obtém a instância viva do `SDLActivity` via `SDL_AndroidGetActivity()` na thread principal e cria uma referência global persistente (`NewGlobalRef`) para a classe Java e o método estático `httpExecute`.
    2. Proteção com `env->ExceptionClear()` em todas as chamadas JNI.
    3. Implementado o despachador `runAsync` que usa `SDL_CreateThread`, garantindo que o SDL faça o `AttachCurrentThread` na inicialização e o `DetachCurrentThread` na finalização de cada thread de background.
- **Escala Responsiva de Alta Resolução para Mobile (High-DPI / Telas Verticais):**
  - *Diagnóstico:* Em smartphones modernos (1080x2400+), o modal de Cloud Save aparecia minúsculo e quase ilegível, pois as dimensões estavam com travas de pixels pensadas apenas para desktop/Switch.
  - *Solução:* Reformulado o layout de `drawModal()` com fator de escala dinâmico `uiScale = std::clamp(baseDim / 480.0f, 1.0f, 2.5f)`. Em orientação retrato (mobile), o modal agora ocupa 94% da largura útil da tela e 64% da altura. As fontes escalam dinamicamente até 42px de altura (garantindo legibilidade cristalina em telas 1080p/1440p) e os botões de ação passam a ter altura de toque ergonômica de até 92px.
## Sessão 26 (Preservação de Timestamp Real do Save RMS e Layout Responsivo Mobile Lado a Lado)
- **Preservação de Timestamp Real de Save em Disco no RMS (Dirty Flag):**
  - *Diagnóstico:* Ao abrir a tela do gerenciador de Cloud Save, o horário do save local aparecia com o horário do momento do acesso ao invés do horário real de quando o jogador salvou o jogo in-game.
  - *Causa Raiz:* No subsistema MIDP/RMS (`src/midp/midp.cpp`), o método `RecordStore_closeRecordStore` abria incondicionalmente um `std::ofstream` e regravava o arquivo `.rms` em disco mesmo quando o RecordStore havia sido aberto exclusivamente para leitura (por exemplo, na tela de título para verificar se o botão "Continuar" deve ficar ativo, ou na verificação de slots). Essa regravação desnecessária atualizava o `st_mtime` (data de modificação) do arquivo no sistema de arquivos para o horário de acesso. Além disso, `getLocalSavesDate()` anteriormente lia o mais recente entre todos os arquivos RMS, incluindo opções de som/vibração (`_o.rms`), que podiam ser alteradas ao navegar nos menus.
  - *Solução:*
    1. Adicionado o campo `bool dirty = false;` à estrutura `RecordStoreObj` em `midp.h`.
    2. Apenas operações efetivas de alteração de dados (`RecordStore_addRecord` e `RecordStore_setRecord`) marcam `rs->dirty = true`.
    3. Em `RecordStore_closeRecordStore`, se `!rs->dirty`, o método retorna imediatamente sem gravar nada no disco. Isso preserva perfeitamente a data e hora originais de quando o save foi gravado pelo jogador.
    4. Em `getLocalSavesDate()`, a resolução de data foi filtrada estritamente para os arquivos reais de campanha dos heróis (`_k.rms`, `_s.rms`, `_w.rms`), ignorando arquivos de configuração.
- **Reformulação do Layout Responsivo Mobile (High-DPI, Paisagem Lado a Lado e Retrato Content-Fitted):**
  - *Diagnóstico:* Em smartphones Android na horizontal (paisagem), o cartão do save local ficava parcialmente cortado e oculto sob os botões de ação inferiores por falta de espaço vertical na proporção 20:9 / 16:9. Na vertical (retrato), havia um grande vácuo escuro vazio entre os cartões e os botões, e as fontes ainda pareciam pequenas para telas 1080p+.
  - *Solução:*
    1. **Orientação Paisagem (Landscape):** Os cartões "Nuvem" e "Local" agora são dispostos **lado a lado** (`cardW = (modalW - 48 - cardMargin) / 2`). Os 4 botões de ação inferiores foram reposicionados em uma **única linha horizontal de 4 botões** (`btnW = (modalW - 48 - 3 * btnMargin) / 4`), utilizando a largura abundante das telas widescreen. Toda a informação cabe com folga vertical, eliminando completamente sobreposições e a necessidade de scroll.
    2. **Orientação Retrato (Portrait):** O cálculo da altura da janela modal (`modalH`) foi transformado em um modelo dinâmico ajustado estritamente à soma dos elementos visíveis (`neededH`), eliminando 100% do vácuo vazio.
    3. **Escala e Tipografia High-DPI:** `uiScale` foi ajustado para escalar até `3.4f` em telas verticais de alta densidade (1080p+), com fontes de cabeçalho de até 56px, fontes informativas de até 42px e cartões de 160px de altura com 2 linhas nítidas de dados (Herói e Data formatada `YYYY-MM-DD HH:MM`).
    4. **Harmonização e Uniformidade Tipográfica dos Botões:** Substituído o cálculo isolado por botão pelo analisador global `calcBtnFont(texts, btnW, btnH, tw, th, tstep)`. O algoritmo avalia a maior string do grupo de botões (`maxLen`) e aplica o mesmo tamanho e espaçamento (`tstep`) a todos os botões do grupo, eliminando a inconsistência onde botões com rótulos longos ficavam comprimidos e curtos ficavam espaçados.
    5. **Paridade High-DPI no Auto-Updater OTA (`src/platform/updater.cpp`):** Aplicada a mesma formulação dinâmica de `uiScale`, altura proporcional ao conteúdo (`neededH`), fontes ampliadas para até 56px e botões de até 96px táteis no modal do atualizador do GitHub, garantindo legibilidade nítida e proporções perfeitas no mobile.
- **Arquitetura de Campanhas Múltiplas e Sincronização Total de Heróis (`_k.rms`, `_s.rms`, `_w.rms`, `_c.rms`):**
  - *Comportamento do Jogo Original:*
    - Cada personagem da campanha possui seu próprio slot RMS independente: Ronin grava em `_k.rms`, Reah grava em `_s.rms` e Aramor em `_w.rms`.
    - Ao zerar a campanha com um herói, o jogo grava em `_c.rms` (Clear save), persistindo flags que desbloqueiam os próximos personagens e modos de jogo. O save da campanha anterior permanece 100% preservado em seu respectivo `.rms`.
    - Os saves de heróis coexistem simultaneamente no sistema de arquivos.
  - *Integração com Cloud Save:*
    - O empacotador atômico JSON (`heroes_lore_save.json`) lê e envia para a nuvem **todos os 5 arquivos de uma vez só** (`_k.rms`, `_s.rms`, `_w.rms`, `_o.rms`, `_c.rms`).
    - O resumo analisa dinamicamente todos os slots existentes (ex: `Ronin Nv 48, Reah Nv 1`).
    - Ao restaurar em outro dispositivo (PC, Switch, Android), todos os heróis existentes e os desbloqueios do `_c.rms` são restaurados conjuntamente, sem que um herói sobrescreva ou apague outro.
- **Redesenho Nobre de Botões em 2 Linhas com Preservação Estrita do Aspect Ratio das Fontes:**
  - *Diagnóstico do Problema:* Quando o texto do botão concatenava atalho e ação em linha única (ex: `[ 1 / A ]: ENVIAR BACKUP`), o comprimento de 24 caracteres forçava a compressão horizontal extrema (`tstep`), enquanto a altura `th` permanecia em 20-24px. Isso produzia caracteres visualmente esticados na vertical ("letras alongadas").
  - *Solução Arquitetural:*
    1. **Separação em 2 Linhas:** Linha 1 exibe o Título da Ação em destaque (`ENVIAR BACKUP`, `RESTAURAR`, `DESCONECTAR`, `FECHAR`) e Linha 2 exibe o atalho físico de console/PC (`[ 1 / A ]`, `[ 2 / X ]`, `[ 3 / Y ]`, `[ B / ESC ]`, ou no Switch `[ A ]`, `[ X ]`, `[ Y ]`, `[ B ]`).
    2. **Preservação de Aspect Ratio 0.60:** `charW` e `charH` agora escalam juntos proporcionalmente caso necessário. A largura do caractere nunca é espremida isoladamente, preservando a proporção geométrica nativa 22x36 da textura de fonte OSD.
    3. **Acabamento Chanfrado Duplo:** Adicionada moldura interna suave nos botões para conferir relevo e profundidade visual refinada.

## Sessão 27 (Engine de Tipografia Vetorial TrueType com stb_truetype, Fonte Inter e Refatoração Moderna do Cloud Save)
- **Causa Raiz da Fonte "Estranha" e Ilegível no Mobile/PC/Switch:**
  - *Diagnóstico:* O usuário relatou que a fonte da janela de saves estava muito feia em todas as plataformas, sem escalar corretamente ("no Flutter nunca tive esse tipo de problema"), com letras em formatos de palito, espaçamentos bizarros e atalhos físicos `[ 1 / A ]` estranhos em telas touch.
  - *Causa Raiz:* Todo o texto do modal dependia da textura bitmap `font_osd.rgba`, que era uma fonte de depuração mono-espaçada fixa de 22x36 por célula. Qualquer letra (mesmo 'i' ou '.') era forçada a ocupar a mesma largura que 'W', e não havia kerning nem suporte a acentos da língua portuguesa em UTF-8. Ao ser escalada ou reduzida, as letras ficavam distorcidas e pixeladas.
  - *Solução com Engine TrueType Integrado (`stb_truetype.h` + Inter-SemiBold):*
    1. **Integração de `third_party/stb_truetype.h`:** Adicionada a biblioteca de domínio público `stb_truetype` em `third_party/stb_truetype.h`, sem qualquer dependência externa ou DLL extra de terceiros.
    2. **Fonte de Alta Definição `Inter-SemiBold.ttf`:** Empacotada a fonte Inter em `assets/fonts/ui_font.ttf`, `android/app/src/main/assets/fonts/ui_font.ttf`, `reference/extracted/fonts/ui_font.ttf` e integrada na cópia do RomFS do Nintendo Switch (`tools/prepare_switch_romfs.py`).
    3. **Rasterização em Atlas de Textura 1024x1024:** Na inicialização, a fonte é assada em uma textura suave de 1024x1024 com antialiasing linear suave, cobrindo todos os caracteres ASCII (32 a 126) e os blocos estendidos Latin-1 (160 a 255).
    4. **Decodificador UTF-8 para Latin-1 (`decodeNextUtf8`):** Adicionado suporte nativo e automático a caracteres acentuados de PT-BR, ES, IT (á, é, í, ó, ú, ç, ã, õ, ñ, etc.).
    5. **Kerning e Largura Proporcional Real (`xadvance`):** Letras finas como 'i', 'l', '.' ocupam sua largura real estreita, e letras largas como 'm', 'w' ocupam seu espaço completo, produzindo uma tipografia com a mesma qualidade de texto do Flutter.
    6. **Novas APIs de Plataforma:** `Platform::drawTextColored(renderer, text, x, y, fontSize, r, g, b, a)` e `Platform::getTextWidth(text, fontSize)`.

- **Refatoração Completa do Modal de Cloud Save (Flutter/Console Grade UI):**
  - *Diferenciação por Orientação (Portrait vs Landscape):*
    - **Retrato (Mobile Vertical, ex: 1080x2400):** O modal agora preenche 94% da largura da tela (`modalW = winW * 0.94f`), dando amplitude e presença visual elegante. Os dois cards de saves ("Nuvem" e "Local") ficam **empilhados verticalmente** com margens generosas. Os botões inferiores são organizados em uma **grade 2x2 ampla e tátil** (`btnH = 54px a 70px`), perfeita para toque confortável com os dois polegares.
    - **Paisagem (Horizontal / Switch / PC / Mobile Deitado):** O modal ocupa 82% da largura útil (`modalW = winW * 0.82f`), os cards ficam **lado a lado** (`w = (contentW - 14) / 2`), e os 4 botões ficam dispostos em uma **barra horizontal uniforme e alinhada** no rodapé.
  - *Diferenciação por Plataforma:*
    - **No Android / Mobile Touch:** Botões são 100% limpos e focados no toque (`ENVIAR BACKUP`, `RESTAURAR`, `DESCONECTAR`, `FECHAR`), eliminando qualquer atalho confuso de teclado físico (`[ 1 / A ]`, etc.).
    - **No Nintendo Switch:** Exibe os botões físicos característicos do Joy-Con: `( A ) ENVIAR BACKUP`, `( X ) RESTAURAR`, `( Y ) DESCONECTAR`, `( B ) FECHAR`.
    - **No Desktop / PC:** Atalhos discretos exibidos em segunda linha.
  - *Calibração de Escala Dinâmica por Resolução Real (Paridade com Mockup IA):*
    - *Diagnóstico:* Embora a janela estivesse perfeita no Windows (720p/960p), em smartphones com telas de alta densidade (1080x2400 / 2400x1080), as travas rígidas de clamp (ex: `max 25px` para texto, `max 76px` para botões e largura horizontal travada em `1150px`) faziam o diálogo parecer pequeno e com botões finos em telas de 6 polegadas.
    - *Solução:* Substituído o clamp rígido pelo fator de escala dinâmico proporcional à resolução real: `scaleFactor = std::max(0.85f, minDim / 720.0f) * 1.15f` no mobile touch.
    - Em 720p (Windows / Switch), a escala se mantém em `1.0f`, preservando 100% da estética aprovada no desktop.
    - Em 1080p móvel (Android), a escala sobe para `~1.72f`, elevando o título para ~43px, botões para ~90px táteis, e cartões para ~136px de altura, atingindo paridade visual exata com os mockups gerados pelo usuário. Em modo paisagem ultrawide (2400x1080), a largura do modal expande até 70% da tela (~1680px), eliminando o aspecto de "caixa espremida".

## Sessão 28 (Alinhamento Tipográfico e Redimensionamento Responsivo do Auto-Updater OTA Multiplataforma - v1.2.2)
- **Causa Raiz de Textos Tortos / Deslocados para a Direita no Modal de Atualização:**
  - *Diagnóstico:* Em todas as plataformas (Android portrait/landscape, Nintendo Switch e PC), o usuário relatou que os textos da tela de atualização ficavam deslocados, fora de centralização ("além de torto"), e que atalhos em botões colidiam com a borda inferior.
  - *Causa Raiz:* `Updater::drawModal` ainda utilizava a API legada `Platform::drawText(renderer, text, x, y, charW, charH, 255, stepX)` e media a largura do texto via `Platform::getTextWidth(text, charW, stepX)`. Como a largura do caractere (`charW = charH * 0.64f`) era enviada como primeiro argumento numérico e interpretada pela engine TrueType como `fontSize`, o cálculo de largura subestimava a extensão real do texto em ~36%. Ao calcular a coordenada inicial `x = box.x + (box.w - tw) / 2`, o texto começava dezenas de pixels à direita do centro e estourava a borda. Além disso, offsets fixos da fonte bitmap antiga (`+ 8`, `- charH + 1`, `+ 3`) empurravam os textos para baixo dos botões.
  - *Solução Definitiva:*
    1. Migração completa de todos os textos do atualizador para a tipografia vetorial TrueType com antialiasing linear e kerning: `Platform::drawTextColored(renderer, text, x, y, fontSize, r, g, b, a)` e medição exata via `Platform::getTextWidth(text, fontSize)`.
    2. Como o mesmo valor exato de `fontSize` é utilizado na medição e na renderização, todas as strings ficam 100% centralizadas tanto no eixo X quanto no eixo Y.
    3. Nos botões de duas linhas (Ação + Atalho de console/PC), implementado o cálculo de altura total e centralização composta (`int totalH = effFontSize + effScSize + 4; int ty1 = bRect.y + (bRect.h - totalH) / 2; int ty2 = ty1 + effFontSize + 4;`), eliminando o atalho colado na borda inferior.

- **Harmonização de Escala e Eliminação de Janelas Monstruosas ou Esmagadas:**
  - *Diagnóstico:* No Nintendo Switch e no PC, a janela modal ocupava quase 85% de toda a tela (`1088px` de largura em 720p), ficando gigante e desproporcional para um simples diálogo de confirmação. No celular, na vertical ficava estreita/esmagada com botões pequenos poluídos com `[ TOQUE ]`, e na horizontal virava uma faixa achatada espremida.
  - *Solução Arquitetural:*
    1. **Nintendo Switch e PC Desktop:** O modal agora é um diálogo nobre, focado e compacto: `modalW = std::clamp((int)(620.0f * scaleFactor), 540, (int)(winW * 0.65f))`. Em 720p nativo do Switch, ocupa exatamente 620x360 (~48% da largura útil), perfeitamente centralizado com backdrop escuro ao redor.
    2. **Mobile Portrait (Vertical, 1080x2400):** Largura de 92% da tela (`winW * 0.92f`), cartões arejados e botões táteis altos de até 86px, confortáveis para os dedos.
    3. **Mobile Landscape (Horizontal Ultrawide, 2400x1080):** Largura proporcional à altura da tela (`winH * 1.05f`), gerando proporção áurea equilibrada de ~1134x580 em 1080p móvel e acabando com a sensação de "faixa achatada".
    4. **Limpeza de Atalhos no Touch:** No Android touch, os botões exibem rótulos limpos (`ATUALIZAR` e `CANCELAR`), eliminando a poluição visual de `[ TOQUE ]`. No Switch, exibem os botões nativos `( A )` e `( B )`. No PC Desktop, exibem `[ A / ENTER ]` e `[ ESC / B ]`.
    5. **Botão Fechar [X] no Cabeçalho:** Adicionado botão dedicado de fechar com hit-test no topo direito, permitindo descarte rápido via touch.
    6. **Suporte a 4 Idiomas:** Todas as mensagens do atualizador agora utilizam a matriz de localização multilíngue (Português, Inglês, Italiano e Espanhol).## Sessão 29 (Migração de Aspect Ratio e FPS para o Menu de Opções e Proporção Automática Dinâmica)

- **Limpeza do Overlay Touch e Integração ao Menu de Opções Nativo (`be.class`):**
  - *Contexto:* Os botões virtuais táteis de proporção (16:9) e taxa de quadros (FPS) ficavam no canto inferior direito da tela em smartphones e Switch, poluindo a visualização e podendo ser acionados acidentalmente durante o combate.
  - *Solução:*
    1. Removidos os botões virtuais `KEY_TOGGLE_TOUCH_ASPECT` e `KEY_TOGGLE_TOUCH_FPS` do `calculateLayout()` tanto no modo retrato quanto paisagem. O rodapé direito agora fica 100% limpo e desobstruído.
    2. Expandido o menu de opções (`be.a:(II)Z` e `be.a:(Ljavax/microedition/lcdui/Graphics;II)V`) de 6 para 8 itens (`cb.var_byte_a = 8`):
       - Item 0: Áudio / Som (Ligado / Desligado)
       - Item 1: Jogo (Velocidade: Lento / Normal / Rápido)
       - Item 2: Textos (Fala: Normal / Rápido)
       - Item 3: Câmera (Ligada / Desligada)
       - Item 4: Idioma (PT-BR, English, Italiano, Español)
       - Item 5: Nuvem (Google Drive Cloud Save)
       - Item 6: Tela / Aspect Ratio (Auto, 3:4, 16:9)
       - Item 7: Taxa de Quadros / FPS (15 FPS, 30 FPS)
    3. Ajustado o espaçamento vertical entre as linhas para `stepY = 18` iniciando em `startY = n3 - 6`, permitindo que todas as 8 linhas caibam com margem segura de mais de 30px dentro do popup modal in-game de 213px de altura.
    4. Setas indicadoras `<` e `>` foram expandidas para cobrir as 8 opções com alinhamento pixel-perfect.

- **Sistema Triplo de Proporção de Tela com Detecção Automática (`AspectMode`):**
  - *Três Modos Oficiais:*
    - `ASPECT_AUTO = 0` (Default): O motor monitora continuamente a orientação da janela/dispositivo (`winW >= winH` -> 16:9 Widescreen; `winH > winW` -> 3:4 Original com Molduras). Se o jogador girar o smartphone ou alternar a orientação TATE no Switch, a resolução interna e as variáveis do motor J2ME se adaptam dinamicamente em tempo de execução sem travar.
    - `ASPECT_ORIGINAL = 1`: Força a proporção 3:4 original (240x320) com molduras temáticas (Soltia Ancestral / Ardósia / Preto).
    - `ASPECT_WIDESCREEN = 2`: Força a expansão de viewport widescreen 16:9 (568x320) com maior campo de visão de mapa.
  - *Persistência:* Salvo no arquivo `hl_settings.ini` via chave `aspect_mode=0|1|2`.
  - *Atalhos Mantidos:* Atalhos de gamepad (SELECT + START, botão GUIDE) e de teclado (F7, F10) agora ciclam suavemente entre as 3 opções (`Auto` -> `3:4` -> `16:9` -> `Auto`), exibindo a respectiva mensagem OSD.

## Sessão 30 (Organização do Cloud Save em Pasta Dedicada no Google Drive e Auto-Migração)

- **Causa da Visibilidade do Arquivo na Raiz do Google Drive:**
  - *Diagnóstico:* O arquivo de backup `heroes_lore_save.json` aparecia visível e solto na pasta raiz do Google Drive do jogador.
  - *Causa Raiz:* O Google OAuth 2.0 Device Flow (RFC 8628 para TVs & Limited Input Devices) rejeita categoricamente o escopo restrito `https://www.googleapis.com/auth/drive.appdata` com `HTTP 400: invalid_scope`. O escopo permitido e seguro é `https://www.googleapis.com/auth/drive.file`. Entretanto, como nenhum diretório pai era informado na criação do arquivo (`POST /drive/v3/files`), a API do Google Drive assume como padrão a pasta raiz (`root`).
- **Solução Arquitetural: Pasta Dedicada e Auto-Migração Transparente:**
  1. **Pasta Dedicada Automática (`getOrCreateAppFolder`):**
     - Ao consultar ou subir um backup, o jogo busca no Drive uma pasta chamada `"Heroes Lore - Wind of Soltia"` (`mimeType='application/vnd.google-apps.folder' and trashed=false`).
     - Caso não exista, o jogo cria a pasta automaticamente via `POST https://www.googleapis.com/drive/v3/files` e armazena seu ID em cache (`s_appFolderId`).
  2. **Uploads Direcionados à Pasta:**
     - Novos arquivos de backup incluem `"parents": [folderId]` no metadado multipart, sendo gravados diretamente dentro da pasta dedicada.
     - Em atualizações de metadados de arquivos existentes, o endpoint recebe o parâmetro `?addParents={folderId}`.
  3. **Auto-Migração de Saves Antigos da Raiz:**
     - Na rotina assíncrona de consulta (`queryCloudBackupAsync`), a requisição agora solicita o campo `parents` na lista de arquivos.
     - Se o save `heroes_lore_save.json` já existir mas estiver localizado na raiz (ou fora da pasta dedicada), o sistema dispara automaticamente uma chamada `PATCH https://www.googleapis.com/drive/v3/files/{id}?addParents={folderId}&removeParents={oldParent}`.
     - O arquivo é movido para dentro da pasta `"Heroes Lore - Wind of Soltia"` preservando o mesmo ID de arquivo, data e integridade, limpando a raiz do Drive do usuário de forma 100% invisível e automática.

## Sessão 31 (Otimização Ergonômica de Tela e Controles Virtuais no Modo Vertical)

- **Eliminação de Sobreposição de Botões sobre a Tela e Diálogos no Modo Vertical (Android / Switch TATE):**
  - *Diagnóstico:* Em smartphones modernos (19.5:9 e 20:9, ex: 1080x2400) e no Nintendo Switch em modo vertical (720x1280), a centralização do jogo 3:4 na altura da janela (`(winH - gameH) / 2`) deixava um vácuo preto desaproveitado no topo e posicionava o terço inferior do jogo diretamente sob o D-Pad e cluster de botões, cobrindo o protagonista, inimigos e especialmente caixas de diálogo / balões de fala dos NPCs. Além disso, o botão `R` no canto superior direito ficava distante do alcance confortável dos polegares.
  - *Solução Arquitetural:*
    1. **Elevação Inteligente da Tela do Jogo:**
       - No modo retrato (`winH > winW`), a tela do jogo é subida para começar logo abaixo dos botões de sistema do topo com margem de segurança (`gameY = std::max(0, std::min(topLimit, (winH - gameH) / 2))`).
       - Libera mais de 250 pixels de espaço 100% limpo na parte inferior do smartphone.
    2. **Rebaixamento Confortável dos Controles Virtuais:**
       - O centro do D-Pad e do cluster de botões de ação foi descido de `winH - winW * 0.42f` para `winH - winW * 0.31f`.
       - Como o jogo subiu e os controles desceram, a sobreposição com o jogo, HUD e caixas de diálogo foi reduzida para praticamente ZERO em smartphones modernos.
    3. **Botão `R BACK` no Cluster de Ação (Preservação Estrita do Arco Ergonômico):**
       - Mantido 100% intacto o posicionamento relativo em arco dos botões clássicos (5 ATK no centro, 1 SKILL e 7 POT na coluna esquerda, 3 SKILL no topo, 9 ITEM no topo-direito).
       - Criada nova textura circular `btn_rsk_round.rgba` (96px, círculo fosco com anel coral `(240, 110, 80)`, rótulo `"R"` e sub-rótulo `"BACK"`).
       - Encaixado na base direita do cluster (`actX + r5 * 1.35f, actY + r5 * 1.15f`), espelhando perfeitamente o botão 7 POT e ficando diretamente abaixo de 9 ITEM, com alcance imediato do polegar direito para cancelar diálogos, fechar janelas e abrir status.
    4. **Topo Simplificado e Elegante com 2 Pílulas:**
       - Com a descida do `R`, o topo agora exibe apenas duas pílulas amplas e equilibradas: `[ MENU ]` à esquerda (26%) e `[ MAPA ]` à direita (74%).


