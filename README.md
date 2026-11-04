# Microsoft Teams Calendar Flyout for Windows 11 (Windhawk Mod)

[![Windhawk Mod](https://img.shields.io/badge/Windhawk-Mod-blue.svg)](https://windhawk.net/)
[![Windows 11](https://img.shields.io/badge/Windows-11%20WinUI-0078D4.svg)](https://www.microsoft.com/windows)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/)
[![Tests](https://img.shields.io/badge/Tests-Passing-brightgreen.svg)]()

Mod para o **Windhawk** que restaura a integração da agenda do **Microsoft Teams** diretamente no menu suspenso de relógio e calendário da barra de tarefas do **Windows 11** (`ShellHost.exe` / `ShellExperienceHost.exe`).

---

## 🎯 O Problema

No Windows 11, a Microsoft redesenhou a bandeja do sistema e o submenu de calendário da barra de tarefas, removendo a visualização de agenda integrada que existia no Windows 10. Para profissionais e equipes que utilizam o Microsoft Teams no dia a dia, verificar horários de reuniões ou entrar em uma chamada exige abrir o aplicativo pesado do Teams ou navegar no portal web, interrompendo o foco do trabalho.

## 🚀 A Solução

Este mod injeta uma agenda moderna, elegante e interativa diretamente abaixo da grade de calendário do Windows 11:
- 📅 **Sincronização em Segundo Plano**: Baixa periodicamente seus eventos do feed iCalendar (`.ics`) oficial do Outlook/Teams sem travar ou congelar a barra de tarefas.
- 🔗 **Detecção Automática do Teams**: Identifica automaticamente reuniões do Microsoft Teams (empresariais, governamentais, pessoais ou via links seguros Outlook SafeLinks).
- ⚡ **Entrar com 1 Clique ("One-Click Join")**: Botão interativo com design Fluent que abre a chamada diretamente no cliente desktop do Microsoft Teams ou no navegador padrão.
- 🟢 **Indicadores de Status em Tempo Real**:
  - `Ao Vivo / Live Now`: Reunião em andamento neste momento.
  - `Em Breve / Starting Soon`: Reunião começando nos próximos 15 minutos.
  - `Futuro / Upcoming`: Reuniões subsequentes do dia.
  - `Passado / Past`: Eventos já concluídos (opcionalmente ocultáveis).
- 🔁 **Expansão de Regras Recorrentes (RRULE)**: Suporte completo a reuniões diárias, semanais (ex: standups às segundas, quartas e sextas) e eventos recorrentes.
- 🌓 **Adaptação Fluente a Temas**: Suporte nativo aos modos Claro (Light) e Escuro (Dark) do Windows 11 com cores e contrastes acessíveis.
- 📆 **Navegação Dinâmica por Data**: Clique em qualquer dia na grade do calendário para ver imediatamente a agenda daquele dia.
- 💾 **Resiliência Offline**: Armazenamento em cache seguro em memória; suas reuniões continuam visíveis mesmo sem conexão à internet.

---

## 📦 Estrutura do Projeto

```text
├── teams-calendar-flyout.wh.cpp     # Mod completo autocontido pronto para o Windhawk
├── TUTORIAL_MOD_WINDHAWK.md        # Guia detalhado passo a passo de configuração e compilação
├── src/
│   ├── calendar_engine/            # Módulo de processamento de calendário (RFC 5545)
│   │   ├── ics_parser.h / .cpp     # Parser iCalendar, unfolding e expansão de recorrências (RRULE)
│   │   ├── teams_extractor.h / .cpp# Extrator e sanitizador de links do Microsoft Teams
│   │   └── meeting_status.h / .cpp # Cálculo temporal (Live, Soon, Future, Past)
│   ├── mod/                        # Núcleo do mod Windhawk
│   │   ├── mod_settings.h / .cpp   # Gerenciamento de configurações do usuário
│   │   ├── calendar_cache.h / .cpp # Cache em memória thread-safe (shared_mutex)
│   │   ├── http_fetcher.h / .cpp   # Worker assíncrono WinINet com timer
│   │   └── teams_calendar_flyout.h # Hooks de ciclo de vida (Wh_ModInit, Wh_ModUninit)
│   └── ui/                         # Apresentação e injeção XAML
│       ├── agenda_view_model.h / .cpp # ViewModels, paletas de tema e ordenação cronológica
│       └── xaml_injector.h / .cpp  # Injetor WinUI na árvore visual do CalendarCenterGrid
├── tests/                          # Suíte de testes unitários e de integração
│   ├── run_tests.ps1               # Executor de testes automatizados via in-process DLL
│   ├── test_ics_parser.cpp         # Testes de parsing RFC 5545 e RRULE
│   ├── test_teams_extractor.cpp    # Testes de extração de links e status
│   ├── test_mod_skeleton_http.cpp  # Testes de cache multithread e ciclo de vida
│   ├── test_agenda_ui.cpp          # Testes de ViewModel e marcação XAML Fluent
│   └── test_one_click_join_guide.cpp # Testes de sanitização de URL e botão de entrada
└── scripts/
    └── generate_bundle.ps1         # Script gerador do bundle autocontido para distribuição
```

---

## 🛠️ Como Instalar e Usar no Windhawk

### Pré-requisitos
1. Windows 11 (build 22H2 ou superior).
2. [Windhawk](https://windhawk.net/) instalado.

### Passo 1: Obter o link ICS do seu calendário
1. Acesse o **Outlook na Web** ([outlook.office.com](https://outlook.office.com)).
2. Clique no ícone de **Configurações** (engrenagem no canto superior direito).
3. Vá em **Calendário** ➔ **Calendários compartilhados**.
4. Na seção **Publicar um calendário**, selecione seu calendário, escolha a permissão (*"Pode exibir todos os detalhes"*) e clique em **Publicar**.
5. Copie o link **ICS** gerado (inicia com `https://outlook.office365.com/.../reachcalendar.ics`).

### Passo 2: Carregar o Mod no Windhawk
1. Abra o aplicativo **Windhawk**.
2. Vá na aba inferior e ative o **Modo de Desenvolvedor** (*Developer Mode*).
3. Clique em **Create Mod** (Criar Mod).
4. No editor de código do Windhawk, cole o conteúdo completo do arquivo [`teams-calendar-flyout.wh.cpp`](./teams-calendar-flyout.wh.cpp).
5. Clique em **Save & Compile** (Salvar e Compilar).
6. Acesse a aba **Settings** (Configurações) do mod no Windhawk:
   - **Outlook / Teams ICS Feed URL**: Cole o link ICS que você copiou no Passo 1.
   - **Refresh Interval (minutes)**: Intervalo de atualização (padrão: 15 minutos).
   - **Show Past Events**: Marque se deseja exibir reuniões passadas do dia atual.
   - **Enable One-Click Join Button**: Mantém o botão de entrada direta ativo.
7. Clique em **Accept** para salvar as configurações.

Pronto! Ao clicar no relógio da barra de tarefas para abrir o calendário do Windows 11, sua agenda do Microsoft Teams será exibida com status em tempo real e botão de entrada direta.

Para um guia com capturas de tela e resolução de problemas, consulte o documento [`TUTORIAL_MOD_WINDHAWK.md`](./TUTORIAL_MOD_WINDHAWK.md).

---

## 🧪 Executando os Testes Automatizados

O projeto conta com mais de 30 testes unitários e de integração cobrindo todas as camadas sem dependência de janelas ativas da interface do usuário.

Para executar toda a suíte de testes no PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1 -Suite all
```

Você também pode executar suítes específicas:
- `-Suite ics`: Testa o parser RFC 5545, decodificação de texto e regras RRULE.
- `-Suite teams`: Testa extração de URLs, SafeLinks e cálculo de status temporal.
- `-Suite mod`: Testa configurações, cache thread-safe e worker HTTP WinINet.
- `-Suite ui`: Testa geração de marcação WinUI XAML e ViewModels.
- `-Suite join`: Testa sanitização de URLs e botão de entrada com 1 clique.

---

## 🔒 Segurança e Privacidade

- **Sem Intermediários na Nuvem**: O mod se comunica diretamente entre o seu computador e o servidor do Microsoft 365 / Outlook via HTTPS. Nenhum dado de calendário transita por servidores de terceiros.
- **Sanitização de Links**: Todas as URLs são validadas contra uma lista estrita de esquemas permitidos (`https://`, `http://`, `msteams:`), impedindo a execução de protocolos maliciosos ou injeção de comandos.
- **Sem Autenticação Insegura**: A leitura é feita via token de feed público/secreto publicado pelo próprio usuário, dispensando o armazenamento de senhas locais.

---

## 📄 Licença

Distribuído sob a licença MIT. Consulte `LICENSE` para mais informações.
