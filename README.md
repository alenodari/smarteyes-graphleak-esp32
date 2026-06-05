# GraphLeak Detectors no ESP32-S3

Este projeto porta para `C++/ESP-IDF` detectores locais da pasta `../python`.

O firmware atual segue as diretrizes dos `AGENTS.md`:

- usa apenas a série temporal local de volume por medidor
- preserva a lógica dos baselines Python:
  - normalização robusta por hora com `median` e `scale`
  - `EWMA` com `alpha=0.25`
  - `CUSUM` unilateral positivo com `drift=0.75`
  - `Page-Hinkley` com `delta=0.05`
  - alarme com `threshold=50.0`
- privilegia portabilidade embarcada:
  - estado online mínimo por medidor: `ewma` e `cusum`
  - atualização por amostra em `O(1)`
  - nenhum buffer/janela longa em RAM
- salva a saída granular do experimento em `SD card`, para posterior pós-processamento no host

## Estrutura

- `main/ewma_cusum_detector.*`
  Núcleo do detector `EWMA + CUSUM`.
- `main/page_hinkley_detector.*`
  Núcleo do detector `Page-Hinkley`.
- `main/graphleak_csv_replay.*`
  Leitura simples do `graphleak_volume_experiments.csv` diretamente do `SD card`, agrupando o CSV por cenário.
- `main/graphleak_stats_loader.*`
  Leitura simples do CSV com `median/scale` por hora para `N2`, `N8` e `N9`.
- `main/main.cpp`
  Executa o replay completo do CSV no `SD card` e salva `predictions_by_sample.csv` também no `SD card`.
- `main/prediction_csv_sink.*`
  Camada genérica de persistência do CSV granular por amostra.
- `main/sdcard_storage.*`
  Inicialização simples do `SDMMC` usando o slot onboard do devkit.
- `assets/graphleak_reference_stats.csv`
  Arquivo-base com os parâmetros robustos por hora que devem ser copiados para o `SD card`.

## Build e flash

```bash
cd /srv/shared/doutorado/leak/graphleak/esp32
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Entrada do experimento

O firmware agora lê diretamente do `SD card` o mesmo dataset derivado usado no baseline Python:

```text
/sdcard/graphleak_volume_experiments.csv
```

e também os parâmetros robustos por hora:

```text
/sdcard/graphleak_reference_stats.csv
```

O arquivo esperado é o derivado da pasta `../python` e deve conter, no mínimo, as colunas:

- `scenario_id`
- `source_file`
- `scenario_type`
- `time_s`
- `hour`
- `V_N2`
- `V_N8`
- `V_N9`
- `label`
- `leak_downstream_N2`
- `leak_downstream_N8`
- `leak_downstream_N9`

O firmware processa os `60` cenários do CSV e, para cada cenário, executa os três medidores locais atuais:

- `local_N2`
- `local_N8_downstream_only`
- `local_N9_downstream_only`

Para cada medidor local, o firmware hoje executa dois detectores:

- `ewma_cusum`
- `page_hinkley`

Assim, a entrada do experimento no `ESP32` passa a ser exatamente a mesma usada no pipeline Python, sem conversão intermediária para um arquivo `.h` gigante.

## Saída do experimento

O firmware grava um arquivo CSV por detector:

```text
/sdcard/ewma_cusum_preds.csv
/sdcard/page_hinkley_preds.csv
```

O objetivo é que o `ESP32` gere apenas a saída granular por amostra. Depois, no host, os mesmos scripts Python da pasta `../python` devem ser usados para derivar:

- `results_by_scenario.csv`
- `summary_by_config.csv`

Isso evita duplicar lógica de avaliação entre firmware e workstation.
