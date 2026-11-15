# 🔖 Guia de Versionamento e Lançamento de Releases

Este documento detalha **todos os locais do código e das configurações de cada plataforma** onde o número de versão do projeto é definido, como cada sistema operacional consome essa informação, e o procedimento recomendado para lançar uma nova versão.

---

## 📌 Tabela Mestra de Arquivos de Versão

| # | Arquivo | Parâmetro / Linha | Plataforma Afetada | Exemplo | Finalidade |
| :-: | :--- | :--- | :--- | :--- | :--- |
| **1** | [`src/platform/updater.h`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/src/platform/updater.h) | `#define HL_VERSION_TAG` | **Todas** (C++ Core) | `"v1.2.0"` | Tag semântica oficial usada pelo Auto-Updater OTA in-app, mensagens de OSD e comparador do GitHub Releases. |
| **2** | [`src/platform/updater.h`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/src/platform/updater.h) | `#define HL_VERSION_NUM` | **Todas** (C++ Core) | `"1.2.0"` | Número de versão limpo sem o prefixo `v`. |
| **3** | [`android/app/build.gradle`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/android/app/build.gradle) | `versionCode` | **Android** | `12` | Número inteiro estritamente incremental exigido pelo instalador do Android (`PackageInstaller`) para identificar atualizações sobrepostas. |
| **4** | [`android/app/build.gradle`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/android/app/build.gradle) | `versionName` | **Android** | `"1.2.0"` | Nome da versão visível ao usuário nas Informações do Aplicativo nas configurações do Android. |
| **5** | [`Makefile.switch`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/Makefile.switch) | `APP_VERSION :=` | **Nintendo Switch** | `1.2.0` | Metadados do arquivo `.nacp` embutidos no binário `heroes_lore.nro`, exibidos abaixo do ícone no **Homebrew Menu (hbmenu)**. |
| **6** | [`tools/patch_credits.py`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/tools/patch_credits.py) | `Versao Atual: {version_tag}` | **In-Game (Todas)** | `"v1.2.0"` | Bytecode Java da classe `bl.class` exibido no menu **Sobre** do jogo. Lê dinamicamente `updater.h`. |
| **7** | **Git / GitHub Tag** | `vX.Y.Z` | **CI/CD & Releases** | `v1.2.0` | Dispara o pipeline de compilação multi-plataforma e publicação automática no GitHub Actions. |

---

## 🔍 Detalhes por Plataforma

### 🪟 Windows / 🐧 Linux (Desktop / Steam Deck)
* O executável C++ lê diretamente a macro `HL_VERSION_TAG` definida em `src/platform/updater.h`.
* O sistema de Auto-Updater (`src/platform/updater.cpp`) consulta a API do GitHub (`/releases/latest`) e compara a tag remota com `HL_VERSION_TAG`. Se a tag remota for superior (via comparação semântica de versões), o jogo oferece a atualização automática em segundo plano.

### 📱 Android
* O Android utiliza dois identificadores no arquivo `android/app/build.gradle`:
  * `versionCode` (inteiro): **Deve ser incrementado em +1 a cada release** (ex: 11 ➔ 12). Se o `versionCode` for menor ou igual ao da versão já instalada no aparelho, o Android recusa a instalação acusando erro de pacote existente.
  * `versionName` (string): Nome legível (ex: `"1.2.0"`).
* O binário nativo `libmain.so` também herda `HL_VERSION_TAG` via C++, mantendo o comparador de atualização OTA alinhado.

### 🎮 Nintendo Switch
* O arquivo `Makefile.switch` compila a ferramenta `nacptool` para gerar o arquivo `.nacp` com os metadados do jogo.
* O campo `APP_VERSION` define a versão exibida na interface do console (Homebrew Menu).
* O executável interno (`heroes_lore.nro`) usa o mesmo módulo C++ de auto-atualização para substituir o NRO no cartão SD (`sdmc:/switch/heroes_lore/`).

### 🕹️ Tela "Sobre" In-Game (`bl.class`)
* O jogo original J2ME exibe a tela de créditos no menu através da classe `bl.class`.
* O script `tools/patch_credits.py` modifica a constante de string UTF-8 e o bytecode de `bl.<init>` diretamente no arquivo binário `.class` (distribuído em `reference/extracted/bl.class`, `build/assets/bl.class` e `android/app/src/main/assets/bl.class`), inserindo a versão atual e o atalho de verificação de atualização.

---

## ⚡ Como Fazer o Ajuste de Versão (Método Rápido / Automatizado)

Foi criado o script [`tools/bump_version.py`](file:///c:/Users/david/Desktop/Projetos_Flutter/heroes_lore_recomp/tools/bump_version.py) para realizar todas as alterações de forma 100% segura e instantânea:

### 1. Consultar a versão atual:
```bash
python tools/bump_version.py --current
```

### 2. Subir para uma nova versão (ex: `1.2.1`):
```bash
python tools/bump_version.py 1.2.1
```

O script automaticamente:
1. Atualiza `HL_VERSION_TAG` e `HL_VERSION_NUM` em `src/platform/updater.h`.
2. Incrementa o `versionCode` em +1 e atualiza o `versionName` em `android/app/build.gradle`.
3. Atualiza `APP_VERSION` em `Makefile.switch`.
4. Executa `tools/patch_credits.py` para sincronizar o bytecode da tela Sobre (`bl.class`).
5. Imprime o resumo com os comandos Git exatos prontos para copiar e colar.

---

## 📋 Checklist Manual de Release

Se optar por fazer o processo manualmente, siga este checklist:

- [ ] 1. Alterar `src/platform/updater.h`:
  ```cpp
  #define HL_VERSION_TAG "vX.Y.Z"
  #define HL_VERSION_NUM "X.Y.Z"
  ```
- [ ] 2. Alterar `android/app/build.gradle`:
  ```groovy
  versionCode <versao_anterior + 1>
  versionName "X.Y.Z"
  ```
- [ ] 3. Alterar `Makefile.switch`:
  ```makefile
  APP_VERSION := X.Y.Z
  ```
- [ ] 4. Atualizar a tela Sobre executando:
  ```bash
  python tools/patch_credits.py
  ```
- [ ] 5. Atualizar a documentação:
  - `README.md` (badges, tabela de controles, destaques se aplicável).
  - `docs/STATUS.md` (checklist do passo correspondente).
  - `docs/LEARNINGS.md` (resumo dos aprendizados da sessão).
- [ ] 6. Commitar e criar a TAG anotada do Git:
  ```bash
  git add .
  git commit -m "chore(release): bump version to vX.Y.Z"
  git tag -a vX.Y.Z -m "Release vX.Y.Z"
  git push origin main
  git push origin vX.Y.Z
  ```
- [ ] 7. Acompanhar a compilação multi-plataforma e release automático no GitHub Actions (`.github/workflows/build.yml`).

## Switch 2

`Makefile.switch2` herda `APP_VERSION` de `Makefile.switch`. O procedimento de bump continua único; não editar uma segunda versão para Switch 2. O workflow multiplataforma gera `heroes_lore_switch2.nro` e inclui o arquivo e SHA-256 na publicação por tag `v*` ou execução manual com criação de release habilitada. O workflow reutilizável de Switch 2 não publica sozinho.
