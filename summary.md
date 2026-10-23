| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 11.74 | 24.99 | 20.16 | 2.13× faster | 11.55 | 16.96 | 20.40 | 1.47× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 11.29 | 21.11 | 19.50 | 1.87× faster | 11.40 | 16.07 | 19.44 | 1.41× faster |
| deep: 768 files, 97 dirs | all files | 4.13 | 6.45 | 4.34 | 1.56× faster | 5.36 | 7.96 | 4.34 | 1.49× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.91 | 4.69 | 4.10 | 1.20× faster | 5.09 | 5.94 | 4.13 | 1.17× faster |
| mixed: 1,452 files, 364 dirs | all files | 5.36 | 7.07 | 7.40 | 1.32× faster | 3.44 | 5.81 | 7.48 | 1.69× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 5.20 | 6.69 | 7.23 | 1.29× faster | 3.39 | 6.27 | 7.29 | 1.85× faster |

Median wall time of 21 warm runs, process startup included. fynd at ec87848; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
