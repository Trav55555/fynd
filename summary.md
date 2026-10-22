| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 12.06 | 25.89 | 21.58 | 2.15× faster | 11.95 | 17.41 | 21.48 | 1.46× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 11.55 | 21.73 | 20.97 | 1.88× faster | 11.74 | 16.40 | 20.99 | 1.40× faster |
| deep: 768 files, 97 dirs | all files | 4.45 | 6.89 | 4.57 | 1.55× faster | 5.66 | 7.93 | 4.58 | 1.40× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 4.18 | 4.95 | 4.28 | 1.18× faster | 5.43 | 6.27 | 4.35 | 1.16× faster |
| mixed: 1,452 files, 364 dirs | all files | 5.67 | 7.53 | 7.80 | 1.33× faster | 3.67 | 6.32 | 7.66 | 1.72× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 5.54 | 7.17 | 7.59 | 1.29× faster | 3.55 | 6.67 | 7.48 | 1.88× faster |

Median wall time of 21 warm runs, process startup included. fynd at b17a341; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
