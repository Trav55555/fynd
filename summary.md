| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 11.47 | 27.18 | 20.08 | 2.37× faster | 11.20 | 17.44 | 20.26 | 1.56× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 11.03 | 21.73 | 19.45 | 1.97× faster | 11.14 | 16.18 | 19.75 | 1.45× faster |
| deep: 768 files, 97 dirs | all files | 4.38 | 6.68 | 4.65 | 1.53× faster | 5.11 | 7.67 | 4.77 | 1.50× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 4.16 | 4.92 | 4.38 | 1.18× faster | 4.88 | 6.13 | 4.61 | 1.25× faster |
| mixed: 1,452 files, 364 dirs | all files | 5.68 | 7.38 | 8.12 | 1.30× faster | 3.52 | 5.74 | 8.18 | 1.63× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 5.51 | 7.14 | 7.90 | 1.30× faster | 3.42 | 6.43 | 8.00 | 1.88× faster |

Median wall time of 21 warm runs, process startup included. fynd at a516b5d; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V74 80-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
