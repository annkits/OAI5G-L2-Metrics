<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# OAI gNB + USRP B200 на Ubuntu (с L2-счётчиками и Grafana)

Пошаговая инструкция: чистая Ubuntu → сборка OAI из ветки `l2-counters` с поддержкой
USRP → проверка B200 → запуск ядра сети, gNB и UE → графики L2-счётчиков в Grafana.

**Содержание**

[[_TOC_]]

## 0. Что понадобится

- **ПК**: Ubuntu 22.04 или 24.04 Desktop (x86_64), установленная на диск, **не WSL и не виртуальная машина**.
  Желательно от 8 ядер, 16+ ГБ RAM. Ноутбук держать **на зарядке**: от батареи CPU троттлится и сыплются ошибки реального времени.
- **USRP B200** (1 канал TX + 1 канал RX), кабель **USB 3** (синий разъём), порт USB 3 прямо на материнской плате, без хабов.
- **UE**, одно из двух:
  - коммерческий UE (смартфон или модем Quectel с поддержкой n78) и программируемая SIM-карта;
  - OAI nrUE на **второй** USRP (B200/B210), лучше на втором ПК.
- **Антенны** или **коаксиальный кабель с аттенюаторами** (30–40 dB). См. раздел о безопасности ниже.

> **Безопасность и законность.** Band n78 (3.3–3.8 ГГц) — лицензируемый диапазон, излучать в эфир
> без разрешения нельзя. Для лабораторных опытов соединяй gNB и UE кабелем через аттенюаторы
> или работай в экранированной камере. **Никогда не соединяй TX напрямую с RX без аттенюатора**:
> максимальная входная мощность RX у B200 около −15 dBm, превышение может сжечь вход.

## 1. Клонирование репозитория

```bash
sudo apt update
sudo apt install -y git
git clone git@github.com:annkits/OAI5G-L2-Metrics.git ~/openairinterface5g
cd ~/openairinterface5g
git checkout l2-counters
git log --oneline -3     # должны быть коммиты про L2 counters и HARQ
```

Если SSH-ключ на этой системе ещё не добавлен в GitHub, клонируй по HTTPS:
`https://github.com/annkits/OAI5G-L2-Metrics.git`.

## 2. Зависимости и UHD

UHD (USRP Hardware Driver) — пользовательская библиотека Ettus, через неё OAI работает с USRP.
Драйвер ядра B200 не нужен: UHD работает через libusb и при старте сам загружает в плату прошивку и FPGA-образ.

### 2.1 Установка через build_oai (рекомендуется)

Ставит все зависимости OAI и UHD из PPA Ettus (`ppa:ettusresearch/uhd`):

```bash
cd ~/openairinterface5g/cmake_targets
./build_oai -I -w USRP
```

Скрипт сам удалит старые пакеты `uhd*`/`libuhd*`, если они есть, подключит PPA и установит
`libuhd-dev`, `uhd-host` и boost.

### 2.2 Образы прошивки и FPGA

```bash
sudo uhd_images_downloader
ls /usr/share/uhd/images | grep b200     # usrp_b200_fw.hex, usrp_b200_fpga.bin
```

### 2.3 Доступ к USB без root

Пакет `uhd-host` кладёт udev-правило. Проверь его наличие и перезагрузи правила:

```bash
ls /usr/lib/udev/rules.d/ /lib/udev/rules.d/ 2>/dev/null | grep uhd   # 60-uhd-host.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 2.4 (Опционально) UHD из исходников

Нужно, только если требуется конкретная версия UHD:

```bash
export BUILD_UHD_FROM_SOURCE=True
export UHD_VERSION=4.8.0.0          # тег из github.com/EttusResearch/uhd без "v"
cd ~/openairinterface5g/cmake_targets
./build_oai -I -w USRP
```

## 3. Проверка B200

Подключи B200 к USB 3 и проверь по шагам:

```bash
lsusb | grep -i ettus               # 2500:0020 Ettus Research LLC USRP B200
uhd_find_devices                    # должен найти type: b200
uhd_usrp_probe                      # загрузит FPGA и выведет дерево устройства
```

В выводе `uhd_usrp_probe` проверь:
- `Operating over USB 3` — **обязательно USB 3**. На USB 2 нужной полосы не будет.
- Нет ошибок про отсутствующие образы.

Тест пропускной способности: имитирует нагрузку, близкую к 106 PRB (46.08 Msps):

```bash
/usr/libexec/uhd/examples/benchmark_rate --args "type=b200" \
  --rx_rate 46.08e6 --tx_rate 46.08e6 --duration 30
```

Путь зависит от версии пакета. Если файла нет, найди его: `dpkg -L uhd-host | grep examples/benchmark_rate`.

В итоге должно быть `Num dropped samples: 0`, `Num overruns detected: 0`, `Num underruns detected: 0`
(допустимы единицы в первые секунды). Если ошибок много, сначала пройди раздел 5
(настройка производительности) и повтори тест, при необходимости на 23.04e6.
Если на 23.04e6 тоже плохо, это проблема ПК или USB, а не OAI.

## 4. Сборка OAI

```bash
cd ~/openairinterface5g
mkdir -p build && cd build
cmake .. -GNinja -DOAI_USRP=ON -DENABLE_TESTS=ON
ninja nr-softmodem nr-uesoftmodem oai_usrpdevif params_libconfig tests
```

Проверки:

```bash
ls liboai_usrpdevif.so                       # драйвер USRP для OAI
ldd liboai_usrpdevif.so | grep uhd           # libuhd.so.4.x => /usr/lib/...
ctest -R test_l2_counters                    # unit-тест счётчиков
```

## 5. Настройка производительности (каждый раз после перезагрузки)

```bash
sudo apt install -y linux-tools-common linux-tools-$(uname -r) cpufrequtils
# CPU на максимальной частоте
for ((i=0;i<$(nproc);i++)); do sudo cpufreq-set -c $i -r -g performance; done
# запретить глубокие C-states (меньше задержек при пробуждении ядер)
sudo cpupower idle-set -D 0
```

Дополнительно:
- закрой браузер, IDE и другие тяжёлые программы;
- в BIOS можно отключить энергосбережение (C-states, Intel SpeedStep/AMD Cool'n'Quiet) и Hyper-Threading;
- (опционально, **снижает защиту**) добавить `mitigations=off` в `GRUB_CMDLINE_LINUX_DEFAULT`
  в `/etc/default/grub`, затем `sudo update-grub` и перезагрузка.

## 6. Ядро сети (OAI CN5G)

### 6.1 Docker

```bash
sudo apt install -y ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}") stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo usermod -a -G docker $(whoami)
# перелогиниться или перезагрузиться
```

### 6.2 Конфигурация и запуск CN5G

Файлы ядра лежат в репозитории, их достаточно скопировать:

```bash
cp -r ~/openairinterface5g/doc/tutorial_resources/oai-cn5g ~/oai-cn5g
cd ~/oai-cn5g
docker compose pull
docker compose up -d
docker ps          # все контейнеры (oai-amf, oai-smf, oai-upf, mysql, ...) в состоянии healthy
```

Сеть ядра — `192.168.70.128/26`, AMF — `192.168.70.132`, хост (gNB) — `192.168.70.129`.
В базе (`~/oai-cn5g/database/oai_db.sql`) уже есть абонент:

| параметр | значение |
|---|---|
| IMSI | `001010000000001` |
| Key | `fec86ba6eb707ed08905757b1bb44b8f` |
| OPc | `C42449363BBAD02B66D16BC975D77CC1` |
| PLMN | MCC `001`, MNC `01` |
| DNN | `oai`, SST 1 |

Остановка: `docker compose down`.

## 7. Мониторинг: InfluxDB + Telegraf + Grafana

```bash
mkdir -p /tmp/l2_counters
cd ~/openairinterface5g/common/utils/l2_counters/monitoring
docker compose up -d
```

Grafana: <http://localhost:3001>, дашборд *OAI → OAI L2 counters*. Смотреть можно без входа,
для редактирования войди как admin/admin.
Подробности о счётчиках — в `common/utils/l2_counters/README.md`.

`/tmp` очищается при перезагрузке, поэтому `mkdir -p /tmp/l2_counters` нужно повторять.

## 8. Запуск gNB на B200

Используется стандартный конфиг B210, он подходит и для B200 (`nb_tx = 1`, `nb_rx = 1`):
`targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf`.
Параметры соты: band n78, 106 PRB, SCS 30 kHz, SSB на 3619.2 МГц (ARFCN 641280), TDD.

```bash
cd ~/openairinterface5g/build
sudo ./nr-softmodem -O ../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf \
  --gNBs.[0].min_rxtxtime 6 -E --continuous-tx \
  --l2_counters.enabled 1 --l2_counters.path /tmp/l2_counters/gnb.json
```

Что означают ключи:
- `-E` — дискретизация 3/4: 46.08 Msps вместо 61.44. **Для B200 обязательно**: 61.44 Msps по USB работает нестабильно;
- `--continuous-tx` — TX работает непрерывно, рекомендуется для B2xx в TDD;
- `--gNBs.[0].min_rxtxtime 6` — даёт UE больше времени на HARQ-ответ, как в официальном туториале;
- `--l2_counters.*` — включает экспорт счётчиков.

Признаки успешного старта в логе:
- `UHD version 4.x.x` и `-- Using calibration table: calib_table_b210_38`;
- `[NGAP] ... NGSetupResponse` — gNB подключился к AMF;
- периодически строки статистики `Frame.Slot ...`. Буквы `U`, `O`, `L` в выводе — признак проблем (см. раздел 11).

Если нагрузка на ПК слишком большая, начни с узкой соты на 24 PRB:
`gnb.sa.band78.fr1.24PRB.usrpb210.conf`. Тогда UE должен использовать тот же `-r` и частоту из этого конфига.

Если подключено несколько USRP, выбери нужную по серийному номеру (номер показывает `uhd_find_devices`):
`--RUs.[0].sdr_addrs "type=b200,serial=XXXXXXX"`.

Остановка: `Ctrl+C`, в логе в конце должно быть `Bye.`.

## 9. Запуск UE

### 9.1 Вариант A: COTS UE (телефон или Quectel)

1. Запрограммируй SIM (Open Cells `program_uicc`) на абонента из базы:
   ```bash
   sudo ./program_uicc --adm 12345678 --imsi 001010000000001 --isdn 00000001 --acc 0001 \
     --key fec86ba6eb707ed08905757b1bb44b8f --opc C42449363BBAD02B66D16BC975D77CC1 \
     -spn "OpenAirInterface" --authenticate
   ```
2. На телефоне: режим только 5G SA (если есть), APN `oai`, ручной выбор сети `00101`.
3. Для Quectel RM500Q: AT-команды из `doc/NR_SA_Tutorial_COTS_UE.md`
   (`AT+CGDCONT=1,"IP","oai"` и т. д.).

### 9.2 Вариант B: OAI nrUE на второй USRP

Лучше запускать на втором ПК: на одном ПК с двумя USRP почти наверняка не хватит CPU и полосы USB.
На втором ПК нужно повторить разделы 1–5. Ядро сети и Grafana на нём не нужны.

```bash
cd ~/openairinterface5g/build
sudo ./nr-uesoftmodem -O ../targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.conf \
  --uicc0.imsi 001010000000001 \
  -r 106 --numerology 1 --band 78 -C 3619200000 --ssb 516 \
  --ue-fo-compensation -E --usrp-args "type=b200" \
  --l2_counters.enabled 1 --l2_counters.path /tmp/l2_counters/ue.json
```

- `-O .../ue.conf` нужен, иначе UE не будет запрашивать PDU-сессию; IMSI в нём переопределяется через `--uicc0.imsi`.
- `--ue-fo-compensation` компенсирует расхождение частот двух USRP (без общего опорного генератора).
- Если счётчики UE тоже нужно видеть в Grafana, а UE работает на другом ПК, скопируй `ue.json`
  на ПК с мониторингом в `/tmp/l2_counters/` (например, `rsync` в цикле) или подними там свой стек мониторинга.

Признаки успеха в логе UE: `Initial sync successful`, `RA procedure succeeded`, `REGISTRATION ACCEPT`,
`PDU SESSION ESTABLISHMENT ACCEPT`, появляется интерфейс `oaitun_ue1`.

## 10. Трафик и проверка счётчиков

```bash
# на хосте UE
ip a show oaitun_ue1                                   # адрес 10.0.0.x
ping -I oaitun_ue1 192.168.70.135                      # oai-ext-dn
# нагрузка
ping -I oaitun_ue1 -i 0.005 -s 1400 192.168.70.135     # ~1–2 Мбит/с в обе стороны
```

Для TCP/UDP-нагрузки используй iperf3 (на `oai-ext-dn` уже запущен `iperf3 -s`):

```bash
iperf3 -c 192.168.70.135 -B <ip_oaitun_ue1> -u -b 20M -t 60         # UL
iperf3 -c 192.168.70.135 -B <ip_oaitun_ue1> -u -b 20M -t 60 -R      # DL
```

Проверка экспорта:

```bash
watch -n1 cat /tmp/l2_counters/gnb.json     # значения должны расти
docker logs -f l2c-telegraf                 # не должно быть ошибок парсинга
```

В отличие от ZMQ, на реальном радиоканале `dl_nack`/`ul_crc_fail` и BLER будут ненулевыми,
это нормально (целевой BLER link adaptation около 10%).

## 11. Траблшутинг

### USRP не находится

| симптом | что делать |
|---|---|
| `lsusb` не показывает `2500:0020` | другой кабель или порт; B200 без внешнего питания может не стартовать на слабом порту, попробуй другой USB-порт на материнской плате |
| `No UHD Devices Found` / `No USRP Device Found` | `uhd_find_devices`; проверь udev (п. 2.3); попробуй с `sudo` — если с sudo работает, дело в правах |
| `LIBUSB_ERROR_ACCESS` | udev-правило не применилось: `sudo udevadm control --reload-rules && sudo udevadm trigger`, переподключи кабель |
| `Could not find the image 'usrp_b200_fpga.bin'` | `sudo uhd_images_downloader`; если UHD собран из исходников, проверь `UHD_IMAGES_DIR` |
| `Operating over USB 2` | другой кабель или порт (синий, USB 3), без хабов и удлинителей |
| `Error: unknown sampling rate` | забыт `-E` или нестандартная ширина полосы |

### Проблемы реального времени

Символы в логе: `U` — underflow (TX не успел), `O` — overflow (RX не успели забрать),
`L` — late (пакет пришёл слишком поздно). Единичные при старте — нормально, постоянный поток — нет.

1. Проверь, что выполнен раздел 5 (governor `performance`, `cpupower idle-set -D 0`), ноутбук на зарядке.
2. `benchmark_rate` (п. 3) без OAI: если и он теряет сэмплы, проблема в ПК или USB.
3. Закрой лишние программы, не запускай сборку параллельно с gNB.
4. Уменьши нагрузку:
   - `--MACRLCs.[0].ul_max_mcs 14` — ограничение UL MCS;
   - `--L1s.[0].max_ldpc_iterations 4` — меньше итераций LDPC-декодера;
   - узкая сота: `gnb.sa.band78.fr1.24PRB.usrpb210.conf`.
5. Меньше логов: `--log_config.global_log_options level,nocolor` или запись вывода в файл, а не в терминал.

### gNB не подключается к AMF

- `docker ps` — все контейнеры CN5G `healthy`?
- `ip a | grep 192.168.70.129` — на хосте должен быть мост docker с этим адресом.
- В логе gNB ошибка про адрес или маску: передай адреса явно, без маски:
  `--gNBs.[0].NETWORK_INTERFACES.GNB_IPV4_ADDRESS_FOR_NG_AMF 192.168.70.129 --gNBs.[0].NETWORK_INTERFACES.GNB_IPV4_ADDRESS_FOR_NGU 192.168.70.129`.
- Логи AMF: `docker logs oai-amf | tail -50` — виден ли gNB.

### UE не видит соту или не подключается

| симптом | что делать |
|---|---|
| UE не находит соту | совпадают ли частота и полоса (`-C 3619200000`, `-r 106`, `--ssb 516`); подключены ли антенны или кабель к правильным портам (TX/RX A); телефон поддерживает n78 и SA |
| синхронизация есть, RA (Msg3) не проходит | timing advance: `-A 45` на UE и/или gNB, подбирать ±5 |
| слабый или перегруженный сигнал, высокий BLER | подстрой `att_tx` (больше — слабее TX) и `max_rxgain` в секции `RUs` конфига или аттенюацию на кабеле |
| `Registration reject` / ошибка аутентификации | IMSI/Key/OPc в SIM или в `ue.conf` должны совпадать с базой (п. 6.2), PLMN 001/01 |
| регистрация есть, PDU-сессии нет | DNN `oai`, SST 1; для nrUE не забыт `-O ue.conf` |
| UE подключился, но нет ping | `ip route` на UE, `docker logs oai-upf`; пингуй именно через `-I oaitun_ue1` |

### Процесс падает при старте

- `mlockall` / `threadCreate` / `pthread_create ... errno 11` — запускай через `sudo`
  (нужны права на realtime-приоритеты и блокировку памяти).
- `cannot write /tmp/l2_counters/gnb.json` — не создана папка: `mkdir -p /tmp/l2_counters`.
- Проверь, какая libuhd реально используется: строка `UHD version` в логе и
  `ldd build/liboai_usrpdevif.so | grep uhd`. Если установлено несколько версий UHD,
  удали лишние или пересобери OAI после установки нужной.

### Grafana пустая

| проверка | команда |
|---|---|
| файл обновляется | `watch -n1 cat /tmp/l2_counters/gnb.json` |
| Telegraf читает | `docker logs l2c-telegraf` |
| данные в Influx | <http://localhost:8086> (admin / `oai-l2-counters`), bucket `l2_counters` |
| диапазон времени | в Grafana выбери «Last 5 minutes» |
| порт 3001 занят | `GRAFANA_PORT=3002 docker compose up -d` |
| конфликт подсети `192.168.72.0/24` | `L2C_SUBNET=192.168.73.0/24 docker compose up -d` |
| файл пишется в другую папку | `L2C_DATA_DIR=/путь docker compose up -d` |

Всплески на графиках скорости при скачке системного времени — известный артефакт:
timestamp в JSON берётся из системных часов.

## 12. Шпаргалка: полный запуск

```bash
# 1. производительность (после каждой перезагрузки)
for ((i=0;i<$(nproc);i++)); do sudo cpufreq-set -c $i -r -g performance; done
sudo cpupower idle-set -D 0

# 2. ядро и мониторинг
cd ~/oai-cn5g && docker compose up -d
mkdir -p /tmp/l2_counters
cd ~/openairinterface5g/common/utils/l2_counters/monitoring && docker compose up -d

# 3. проверка B200
uhd_find_devices

# 4. gNB
cd ~/openairinterface5g/build
sudo ./nr-softmodem -O ../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf \
  --gNBs.[0].min_rxtxtime 6 -E --continuous-tx \
  --l2_counters.enabled 1 --l2_counters.path /tmp/l2_counters/gnb.json

# 5. UE (телефон или nrUE, раздел 9), трафик (раздел 10), Grafana: http://localhost:3001
```

Остановка: `Ctrl+C` в gNB и UE, затем `docker compose down` в `~/oai-cn5g` и в `monitoring/`.
