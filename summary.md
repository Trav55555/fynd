| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 12.15 | 25.72 | 20.87 | 2.12× faster | 11.58 | 17.00 | 20.17 | 1.47× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 11.75 | 21.93 | 20.52 | 1.87× faster | 11.61 | 16.30 | 19.43 | 1.40× faster |
| deep: 768 files, 97 dirs | all files | 4.32 | 7.33 | 4.48 | 1.70× faster | 5.50 | 8.94 | 4.39 | 1.63× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 4.03 | 4.96 | 4.22 | 1.23× faster | 5.29 | 5.40 | 4.19 | 1.02× faster |
| mixed: 1,452 files, 364 dirs | all files | 5.45 | 7.34 | 7.58 | 1.35× faster | 3.55 | 6.40 | 7.43 | 1.80× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 5.32 | 6.98 | 7.40 | 1.31× faster | 3.37 | 6.49 | 7.27 | 1.92× faster |

Median wall time of 21 warm runs, process startup included. fynd at 02e8d6d; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
