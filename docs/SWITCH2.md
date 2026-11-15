# Heroes Lore: Wind of Soltia — Switch 2 experimental

Compilação separada da base 1.2.8 para kaiHEN/ounce-hbmenu. A lógica do jogo, os arquivos RMS e as funções Google Drive permanecem preservados. O workflow multiplataforma e o Makefile de Switch 1 não são alterados.

## GitHub Actions

O workflow **Experimental Nintendo Switch 2 NRO** usa `devkitpro/devkita64:20260219`, o instalador ounce-devkit na revisão `0f38ba80d7ee768c5749278f127749d14ece2036` e o SDK `20261115`. O instalador verifica o SHA-256 do SDK. É o mesmo conjunto que compilou o Heroes Lore Zero e teve execução, backup e restauração Google Drive aprovados no console.

Mudanças relevantes na branch `codex/switch2-action` disparam somente esta compilação experimental. O arquivo também contém `workflow_dispatch` para execução manual quando disponível no GitHub. Não publica releases, cria tags ou integra mudanças à main.

O artefato `heroes_lore_switch2_nro` contém `heroes_lore_switch2.nro`, SHA-256, relatório e este guia. O NRO inclui ícone, NACP e RomFS. Diagnósticos são publicados separadamente no artefato de logs, inclusive se a compilação falhar.

## Adaptação

`Makefile.switch2` usa `libounce/switch2_rules`, `SWITCH2_CPU` e objetos em `build_switch2`. As regras do SDK priorizam OunceVK para os gráficos, mantendo portlibs compatíveis de SDL2, áudio e rede. O formato de saída continua sendo NRO.

O atualizador seleciona apenas `heroes_lore_switch2.nro` quando compilado com `__SWITCH2__`, impedindo a seleção de atualizações binárias de Switch 1. A inicialização de rede e as rotinas Google Drive continuam presentes. O sucesso no Zero não substitui os testes desta build.

## Saves

Os caminhos de armazenamento e os arquivos `_k.rms`, `_s.rms`, `_w.rms`, `_o.rms` e `_c.rms` são preservados. O arquivo de nuvem continua `heroes_lore_save.json`, na pasta Google Drive específica do Wind of Soltia. Não reutilizar a identidade do Heroes Lore Zero nem renomear seus saves para este jogo.

Para validar, preserve cópias do progresso existente e teste criar save, fechar/reabrir, enviar backup ao Google Drive, restaurar e carregar o progresso. A compilação confirma estrutura e recursos do NRO; a compatibilidade dos serviços, áudio e saves requer teste no console.

## Netloader

Com hbmenu ativo, abra Netloader e envie o NRO por nxlink usando o IP do console. Não é necessário reabrir SmileBASIC ou repetir a conexão a cada envio. O jogo contém seus próprios recursos no RomFS; arquivos adicionais do jogo não precisam ser copiados separadamente.

O envio deve usar um nome distinto do Zero e da build de Switch 1. Antes de transferir, confirme que o console voltou à espera do Netloader. Verifique abertura, imagem, controles, áudio, progresso, nuvem e retorno ao hbmenu.

Créditos: [ounce-devkit](https://github.com/ouncebrew/ounce-devkit), [libounce](https://github.com/ouncebrew/libounce), [OunceVK](https://github.com/ouncebrew/ouncevk), [kaiHEN](https://github.com/ouncebrew/kaihen), devkitPro e contribuidores.
