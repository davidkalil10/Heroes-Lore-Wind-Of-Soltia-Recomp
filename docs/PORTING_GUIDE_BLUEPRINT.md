# 🗺️ Guia Mestre de Portabilidade & Recompilação J2ME (Blueprint C++17 + SDL2)

Este documento é o **manual definitivo e guia arquitetural completo** para portar qualquer jogo clássico de celular J2ME (`.jar`) para uma base de código nativa C++17 multiplataforma (PC Windows/Linux, Android, Nintendo Switch, Steam Deck e Web), mantendo **100% de fidelidade lógica, matemática e estética ao original**.

Construído a partir de todos os aprendizados, sucessos e desafios superados durante a recompilação bem-sucedida de *Heroes Lore: Wind of Soltia*.

---

## 🧭 1. Princípios Fundamentais & Filosofia

1. **Fidelidade Literal ao Bytecode:**
   - **Nunca adivinhar regras ou fórmulas "no olhômetro":** fórmulas de dano, IA de inimigos, taxas de drop, tabelas de eventos e colisões devem ser extraídas e executadas exatamente como escritas nos bytecodes `.class` originais.
   - O projeto executa o bytecode diretamente ou transcreve método a método, mantendo identificadores originais em comentários.
2. **Abstração Fina de Plataforma (HAL):**
   - O núcleo do jogo e da JVM não conversa diretamente com o sistema operacional.
   - Toda interação passa por interfaces limpas em `src/platform/` (Renderização, Áudio, Entrada, Armazenamento persistente e Rede).
3. **O Marco Zero (Milestone 1 — Inegociável):**
   - Antes de pensar em Android, Switch, Cloud Save ou Widescreen, o primeiro objetivo absoluto é: **O jogo abrindo no PC (Windows/Linux), 100% jogável via teclado e gamepad, com áudio funcionando e salvando/carregando o progresso perfeitamente.**
4. **Documentação Viva e Contínua:**
   - Toda descoberta, armadilha superada ou decisão de arquitetura é imediatamente registrada em `docs/LEARNINGS.md`.
   - O progresso de cada etapa atualiza `docs/STATUS.md`.

---

## 🧰 2. Ambiente de Desenvolvimento Recomendado

| Ferramenta | Finalidade | Plataforma | Instalação |
| :--- | :--- | :--- | :--- |
| **MSYS2 UCRT64** | Toolchain C++ nativo (GCC 13+, GDB) | Windows | `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb` |
| **CMake & Ninja** | Gerador e executor de build ultra-rápido | Todas | `pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja` |
| **SDL2 & SDL2_mixer** | Renderização 2D, janelas, entrada e áudio | Todas | `pacman -S mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_mixer` |
| **JDK 17 ou 21** | Auditoria de bytecode (`javap`) | Todas | Temurin / OpenJDK |
| **Python 3.10+** | Scripts de pipeline, assets e RomFS | Todas | Com biblioteca `Pillow` instalada |
| **CFR Decompiler** | Decompilação precisa de código ofuscado | Todas | `cfr.jar` |
| **devkitPro (devkitA64)** | Port Homebrew Nintendo Switch | Opcional | Container Docker oficial ou devkitPro pacman |
| **Android NDK 26+** | Port Nativo Android (C++ / Gradle) | Opcional | Android Studio ou CLI NDK |

---

## 📁 3. Estrutura Padrão de Diretórios (Scaffolding)

Ao iniciar o novo projeto para o próximo jogo, organize a raiz exatamente assim:

```
novo_projeto_recomp/
├── AGENTS.md                   # Contrato de trabalho e regras de ouro (leia primeiro em toda sessão)
├── README.md                   # Vitrine do projeto, status, downloads e controles
├── CMakeLists.txt              # Script mestre de compilação C++17
├── Makefile.switch             # Script de compilação devkitPro para Nintendo Switch (.nro)
│
├── docs/                       # Documentação técnica viva
│   ├── STATUS.md               # Checklist detalhado de cada fase do projeto
│   ├── LEARNINGS.md            # Diário de bordo: armadilhas resolvidas e decisões técnicas
│   ├── CLASS_MAP.md            # Mapeamento das classes do jogo (.class -> funcionalidade)
│   ├── VERSIONING.md           # Tabela de sincronização de versão multi-plataforma
│   └── PORTING_GUIDE_BLUEPRINT.md # Este guia
│
├── reference/                  # Material original de consulta (somente leitura)
│   ├── jogo_original.jar       # O arquivo JAR puro
│   ├── extracted/              # Conteúdo descompactado do JAR (imagens, mapas, sons, .class)
│   ├── decompiled_cfr/         # Classes Java decompiladas via CFR com --renamedupmembers
│   └── javap/                  # Desmontagem de bytecode via javap -c -s -p -v
│
├── src/                        # Código-fonte da recompilação nativa C++17
│   ├── vm/                     # Motor CLDC 1.1 (Class Loader, Interpretador, GC, Tipos, Natives)
│   │   ├── vm.h / vm.cpp       # Leitor de .class, constante pool, resolução de campos/métodos
│   │   ├── interp.cpp          # Interpretador das opcodes de bytecode
│   │   └── natives.cpp         # Implementação das APIs fundamentais (java/lang, java/io, java/util)
│   ├── midp/                   # Camada de emulação J2ME MIDP 2.0
│   │   ├── midp.h / midp.cpp   # Display, Canvas, Graphics, Image, RecordStore (RMS), Font
│   │   └── stb_image.h         # Parser de imagens PNG/JPG embutido (domínio público)
│   ├── platform/               # Camada de hardware e abstração de sistema (HAL)
│   │   ├── platform.h          # Interface universal (janela, áudio, persistência, inputs)
│   │   ├── platform_sdl.cpp    # Implementação acelerada SDL2 (render loop, controles, OSD)
│   │   ├── midi_synth.h/.cpp   # Sintetizador MIDI em tempo real (TinySoundFont + TimGM6mb)
│   │   ├── cloud_save.h/.cpp   # Sincronização Google Drive (RFC 8628 Device Flow)
│   │   └── updater.h/.cpp      # Auto-updater OTA in-app via GitHub Releases
│   └── main.cpp                # Ponto de entrada nativo, inicialização do MIDlet e game loop
│
├── assets/                     # Assets distribuídos com o binário
│   ├── ui/                     # Texturas de botões táteis, D-Pad e ícones
│   ├── fonts/                  # Fonte TrueType (Inter-SemiBold.ttf) para modais modernos
│   └── soundfont/              # Banco de áudio MIDI (TimGM6mb.sf2)
│
├── tools/                      # Ferramentas auxiliares automatizadas em Python
│   ├── cfr.jar                 # Decompilador Java CFR
│   ├── bump_version.py         # Sincronizador de versão multi-plataforma
│   ├── generate_gamepad_textures.py # Gerador procedural de botões com antialiasing
│   └── prepare_switch_romfs.py # Empacotador automático da partição RomFS do Switch
│
├── android/                    # Projeto nativo Android (Gradle + NDK)
└── switch/                     # Metadados e ícone oficial do Nintendo Switch (256x256 icon.jpg)
```

---

## 🔍 4. Fase 1 — Extração, Auditoria & Decompilação do JAR

### 1.1 Extração do Arquivo JAR
Um arquivo `.jar` é essencialmente um arquivo ZIP. Extraia seu conteúdo:
```bash
mkdir -p reference/extracted
unzip -q reference/jogo_original.jar -d reference/extracted
```

### 1.2 Decompilação com CFR (com resolução de membros duplicados)
Jogos J2ME frequentemente usam ofuscadores (ProGuard, Zelix, Allatori). Use a flag essencial `--renamedupmembers true`:
```bash
mkdir -p reference/decompiled_cfr
java -jar tools/cfr.jar reference/jogo_original.jar \
  --outputdir reference/decompiled_cfr \
  --renamedupmembers true \
  --caseinsensitivefs true
```

### 1.3 Desmontagem de Bytecode com `javap`
Em caso de dúvidas na decompilação, o bytecode bruto do `javap` é a verdade absoluta:
```bash
mkdir -p reference/javap
for f in reference/extracted/*.class; do
  name=$(basename "$f" .class)
  javap -c -s -p -v -cp reference/extracted "$name" > "reference/javap/${name}.javap"
done
```

### 1.4 Mapeamento Inicial no `docs/CLASS_MAP.md`
Analise o `META-INF/MANIFEST.MF` para encontrar a classe principal:
```
MIDlet-1: Heroes Lore,/icon.png,MainMIDlet
```
Localize e documente no `docs/CLASS_MAP.md`:
1. **Classe do MIDlet:** Extende `javax.microedition.midlet.MIDlet`. Cria e exibe o Canvas.
2. **Classe do Canvas Principal:** Extende `javax.microedition.lcdui.Canvas` ou `FullCanvas`. Contém o game loop, `paint(Graphics g)`, `keyPressed(int keyCode)`.
3. **Motor de Mapa / Tilemap:** Carrega arquivos de blocos/tiles (`.tdf`, `.evt`, `.map`).
4. **Persistência / Save Game:** Usa `javax.microedition.rms.RecordStore`.
5. **Subsistema de Áudio:** Usa `javax.microedition.media.Manager` ou `com.nokia.mid.sound.Sound`.

---

## 🧠 5. Fase 2 — O Motor JVM CLDC 1.1 em C++17 (`src/vm/`)

### 5.1 O Leitor de Classes (`.class` Parser)
Implemente em `vm.cpp` a leitura binária das seções da JVM:
- **Magic:** `0xCAFEBABE`.
- **Constant Pool:** `CONSTANT_Utf8`, `CONSTANT_Integer`, `CONSTANT_Float`, `CONSTANT_Class`, `CONSTANT_String`, `CONSTANT_Fieldref`, `CONSTANT_Methodref`, `CONSTANT_InterfaceMethodref`, `CONSTANT_NameAndType`.
- **Campos e Métodos:** Leitura de flags de acesso, nomes, descritores de tipo e atributos (`Code`).

> [!CRITICAL]
> **Armadilha Mortal de Ofuscação J2ME: Colisão de Nomes por Tipo!**
> Em código Java ofuscado, uma mesma classe pode declarar:
> ```java
> public int a;
> public boolean a;
> public Object a;
> ```
> O bytecode JVM diferencia campos e métodos pelo par **`(Nome, Descritor)`**, NÃO apenas pelo nome!
> **Regra Obrigatória:** Em C++, armazene campos e métodos mapeados pela chave combinada:
> ```cpp
> std::string key = name + ":" + descriptor; // Ex: "a:I" diferente de "a:Z"
> ```

### 5.2 Estrutura do Frame de Execução e Valores
Represente os valores da JVM de forma compacta e eficiente em memória:
```cpp
struct Value {
  union {
    int32_t i;
    int64_t j;
    float f;
    double d;
    struct Object* obj;
  };
};

struct Frame {
  Method* method;
  const uint8_t* pc;
  Value* locals;
  Value* stack;
  int sp; // Stack pointer
};
```

### 5.3 O Interpretador de Instruções (`interp.cpp`)
Concentre-se em implementar com perfeição as instruções CLDC 1.1 realmente utilizadas pelo jogo:
- **Constantes:** `iconst_*`, `bipush`, `sipush`, `ldc`, `aconst_null`.
- **Aritmética e Bits:** `iadd`, `isub`, `imul`, `idiv`, `irem`, `ishl`, `ishr`, `iushr`, `iand`, `ior`, `ixor`.
- **Controle de Fluxo:** `ifeq`, `ifne`, `iflt`, `ifge`, `ifgt`, `ifle`, `if_icmpeq`, `if_icmpne`, `goto`, `tableswitch`, `lookupswitch`.
- **Objetos e Arrays:** `new`, `newarray`, `anewarray`, `arraylength`, `iaload`, `iastore`, `aaload`, `aastore`, `baload`, `bastore`, `caload`, `castore`.
- **Invocação:** `invokevirtual`, `invokestatic`, `invokespecial`, `invokeinterface`.

### 5.4 Reentrância do GIL (Global Interpreter Lock)
Ao integrar hooks nativos em C++ que são chamados pelo próprio loop da VM:
```cpp
class VM {
  std::recursive_mutex gil;
  std::thread::id gilOwner;
public:
  bool isGilOwner() const { return std::this_thread::get_id() == gilOwner; }
  void gilLock() {
    gil.lock();
    gilOwner = std::this_thread::get_id();
  }
  void gilUnlock() {
    gilOwner = std::thread::id();
    gil.unlock();
  }
};
```
Isso impede que eventos nativos ou trocas de tela congelem a aplicação em deadlock.

---

## 🎨 6. Fase 3 — Camada MIDP 2.0 / LCDUI Mínima (`src/midp/`)

### 6.1 `Graphics` 2D
O motor gráfico deve manter um buffer RGBA de pixels (ex: 240x320) sobre o qual as operações de desenho ocorrem:
- `setColor(int RGB)`
- `fillRect(int x, int y, int w, int h)`
- `drawRect(int x, int y, int w, int h)`
- `drawLine(int x1, int y1, int x2, int y2)`
- `setClip(int x, int y, int w, int h)` e `clipRect(int x, int y, int w, int h)`
- `drawImage(Image img, int x, int y, int anchor)`
  - Calcular o offset das âncoras J2ME:
    - Horizontal: `LEFT (4)`, `RIGHT (8)`, `HCENTER (1)`
    - Vertical: `TOP (16)`, `BOTTOM (32)`, `VCENTER (2)`
- `drawRegion(Image src, int x_src, int y_src, int width, int height, int transform, int x_dest, int y_dest, int anchor)`
  - Suportar transformações de sprites: `TRANS_MIRROR`, `TRANS_ROT90`, `TRANS_ROT180`, `TRANS_ROT270`, etc.

### 6.2 `RecordStore` (RMS) com Persistência em Disco
A persistência J2ME é baseada em Records identificados por IDs numéricos (1, 2, 3...):
- Armazene cada RecordStore como um arquivo binário simples na pasta `rms/` (ex: `_k.rms`, `_s.rms`).
- Formato sugerido em disco:
  ```
  [uint32 numRecords]
  [uint32 nextRecordId]
  Para cada record:
    [uint32 id]
    [uint32 dataLen]
    [bytes data]
  ```

> [!CRITICAL]
> **Lição Fundamental de Integridade RMS (A Flag `dirty`):**
> Nunca regrave o arquivo em disco no `RecordStore.closeRecordStore()` se ele foi apenas aberto para leitura!
> - Mantenha uma flag `bool dirty = false;` no objeto `RecordStore`.
> - Só marque `dirty = true;` em chamadas `addRecord`, `setRecord` ou `deleteRecord`.
> - Ao fechar com `!dirty`, retorne imediatamente sem abrir stream de gravação.
> - Isso preserva a data/hora original do arquivo (`st_mtime`) no disco, permitindo exibir a data real do save!

### 6.3 Desempacotamento Universal de Streams Java (`ByteArrayOutputStream`)
Muitos jogos J2ME gravam dados empacotando streams encadeadas (`DataOutputStream` sobre `ByteArrayOutputStream`).
- Implemente uma função recursiva `getUnderlyingBaos(OutputStream* os)` que desempacota o buffer binário original independentemente de quantas camadas de encapsulamento foram usadas no bytecode.

---

## 💻 7. Fase 4 — Camada de Plataforma SDL2 & Validação no PC (Marco 0)

### 7.1 Loop de Renderização e Frame Pacer
O jogo original dependia do timing do processador móvel. No PC moderno a 240Hz, sem limitador o jogo rodaria acelerado.
Implemente um Frame Pacer de precisão sub-milissegundo com `SDL_GetPerformanceCounter()`:
```cpp
void runFramePacer(int targetFps) {
  static uint64_t lastTime = SDL_GetPerformanceCounter();
  uint64_t freq = SDL_GetPerformanceFrequency();
  double targetSec = 1.0 / (double)targetFps;

  uint64_t now = SDL_GetPerformanceCounter();
  double elapsed = (double)(now - lastTime) / (double)freq;
  double remain = targetSec - elapsed;

  if (remain > 0.002) {
    SDL_Delay((Uint32)((remain - 0.001) * 1000.0));
  }
  // Spin-wait preciso para o último milissegundo:
  while ((double)(SDL_GetPerformanceCounter() - lastTime) / (double)freq < targetSec) {
    // yield CPU
  }
  lastTime = SDL_GetPerformanceCounter();
}
```

### 7.2 Entrada Contínua (Typematic Controls para PC)
Em celulares J2ME de 2007, segurar o direcional dependia do hardware do teclado emitindo repetições.
Em C++, implemente controle fluido:
- No `SDL_KEYDOWN`: envia `keyPressed` imediatamente e marca `held = true`.
- Após um delay de repetição inicial (ex: 160ms), emite repetições suaves a cada 40ms (~25Hz) enquanto a tecla estiver mantida.
- No `SDL_KEYUP`: cancela imediatamente o estado e emite `keyReleased`.

### 7.3 Áudio MIDI de Alta Fidelidade com TinySoundFont
Integre `third_party/tsf.h` e `third_party/tml.h` (código em domínio público de arquivo único):
1. Carregue o SoundFont GM `TimGM6mb.sf2` (~5.7 MB) na memória.
2. Ao reproduzir uma música, inspecione os primeiros 4 bytes do buffer. Se forem `MThd`, é um arquivo MIDI padrão:
   - Carregue via `tml_load_memory`.
   - Conecte ao mixer SDL via `Mix_HookMusic`.
   - Renderize blocos PCM estéreo 44.1kHz 16-bit diretamente para o dispositivo de saída.

### 7.4 Checklist de Homologação do Marco 0
Antes de avançar para qualquer outra etapa, valide e celebre:
- [ ] Jogo compila via CMake + Ninja no Windows e Linux com 0 erros.
- [ ] O executável abre e exibe a tela de título com a música MIDI tocando limpa.
- [ ] Consegue iniciar uma nova partida usando teclado (WASD / Setas / Espaço) ou gamepad.
- [ ] Movimentação e combate fluem perfeitamente a 15 FPS e 30 FPS.
- [ ] O jogo salva o progresso e recarrega após reiniciar o executável.

---

## 🚀 8. Roteiro de Expansões Futuras (Passo a Passo)

Uma vez que o Marco 0 esteja homologado, siga a ordem comprovada de expansões:

### Passo 1: Suporte Completo a Gamepad com Vibração (Rumble)
- Use `SDL_GameController` para mapeamento automático (Xbox, DualSense, Switch Pro, 8BitDo).
- Adicione feedback de impacto com `SDL_GameControllerRumble()`.

### Passo 2: True Widescreen (16:9) e Molduras Temáticas (Bezels)
- **Viewport Widescreen:** Altere a largura do canvas de 240 para 568 (renderizando mais colunas de tiles sem esticar sprites).
- **Bezels (3:4):** Crie texturas de molduras ricas em ardósia/pedra e medalhões para preencher as abas laterais em telas panorâmicas.
- **Hook Nativo de Cenário:** Adicione hook C++ em classes como `aj` (desenho de objetos) recalculando o culling dinâmico contra a largura ativa para que baús e plantas não desapareçam nas laterais.
- **Modo Auto:** Detecte dinamicamente a proporção da tela/janela: horizontal aplica 16:9, vertical aplica 3:4 com molduras.

### Passo 3: Port Nativo para Android
- Projeto em `android/` com NDK 26, CMake e `SDLActivity`.
- Implemente `Virtual Gamepad Fullscreen Overlay`:
  - Controles translúcidos estilo *frosted glass*.
  - No modo vertical, eleve a tela do jogo logo abaixo das pílulas superiores e abaixe os botões para a base, eliminando sobreposições com balões de fala de NPCs.
  - Botão `R BACK` circular no cluster de ação inferior direito.
  - Ícone oficial de aplicativo em todas as densidades (`mipmap-xhdpi` etc.).
  - Keystore permanente para atualizações de APK sem conflito de assinatura.

### Passo 4: Port Homebrew para Nintendo Switch (`.nro`)
- Build via devkitPro / devkitA64 com `Makefile.switch`.
- Camada de compatibilidade libnx:
  - Inicialização de RomFS (`romfsInit()`).
  - Loop do SO via `appletMainLoop()`.
  - Saves no cartão SD fora do RomFS (`sdmc:/switch/nome_do_jogo/`).
  - Suporte a modo vertical (TATE 90° e 270° Flip Grip).
  - Ícone de 256x256 e metadados `.nacp`.

### Passo 5: Tipografia Vetorial TrueType (`stb_truetype.h` + Inter)
- Integre `stb_truetype.h` sem dependências externas.
- Asse a fonte `Inter-SemiBold.ttf` em atlas 1024x1024 na inicialização.
- Decodificador UTF-8 para Latin-1 com suporte total a acentos em Português e Espanhol.
- Proporciona texto nítido estilo Flutter em qualquer resolução e densidade de tela.

### Passo 6: Auto-Updater OTA In-App via GitHub Releases
- Detecção automática de novas versões comparando tags semânticas via GitHub API.
- Download direto do pacote zip ou executável por plataforma (WinHTTP no Windows, libcurl no Switch/Linux, PackageInstaller no Android).
- No Windows: substituição atômica via `CreateProcessA` evitando travas de compartilhamento de DLLs em execução.
- No Switch: escrita atômica com liberação de RomFS (`romfsExit()`) e renomeação `.old`.

### Passo 7: Cloud Save Multiplataforma (Google Drive RFC 8628)
- Autenticação sem navegador web via Device Flow (`google.com/device` com código na tela).
- Escopo seguro e aprovado: `https://www.googleapis.com/auth/drive.file`.
- Organização em pasta dedicada **"Nome do Jogo"** no Google Drive via API v3 (`mimeType='application/vnd.google-apps.folder'`).
- Auto-migração silenciosa de arquivos soltos na raiz para a pasta dedicada via `PATCH ?addParents=...&removeParents=...`.
- Empacotamento atômico de todos os arquivos RMS em um único JSON estruturado em Base64.
- Recarregamento da VM despachado com segurança na thread principal.

---

## 📜 9. Tabela de Sincronização de Versionamento

Mantenha o script `tools/bump_version.py` configurado para atualizar em lote com um único comando (`python tools/bump_version.py X.Y.Z`):

| # | Arquivo | Parâmetro | Plataforma |
| :-: | :--- | :--- | :--- |
| **1** | `src/platform/updater.h` | `HL_VERSION_TAG` / `HL_VERSION_NUM` | C++ Core (Todas) |
| **2** | `android/app/build.gradle` | `versionCode` (+1) e `versionName` | Android |
| **3** | `Makefile.switch` | `APP_VERSION :=` | Nintendo Switch |
| **4** | `bl.class` (Tela Sobre) | Bytecode binário da tela de créditos | In-Game |
| **5** | Git Tag | `vX.Y.Z` | GitHub Actions Releases |

---

## 🏆 10. Resumo das Regras de Ouro

1. **A Verdade está no JAR:** Se algo parecer com bug ou comportamento estranho, compile o mesmo cenário e teste no emulador J2ME (*FreeJ2ME* ou *KEmulator*). O comportamento do C++ deve ser idêntico ao do emulador.
2. **Nomes Ofuscados em Comentários:** Ao nomear funções ou variáveis decompiladas em C++, sempre mantenha o identificador original em comentário (ex: `void updatePhysics() /* n.f */`). Isso permite cruzar referências no `javap` a qualquer momento.
3. **Thread Safety Primeiro:** Recursos que afetam a memória da JVM ou do motor de áudio nunca devem ser chamados a partir de threads secundárias desvinculadas.
4. **Mantenha os Documentos Vivos:** Ao superar um problema difícil, gaste 5 minutos documentando em `docs/LEARNINGS.md`. Esse conhecimento economizará dezenas de horas no próximo projeto!
