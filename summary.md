| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 8.17 | 19.34 | 13.85 | 2.37× faster | 8.57 | 13.17 | 17.92 | 1.54× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 7.82 | 16.58 | 13.52 | 2.12× faster | 8.40 | 12.64 | 17.24 | 1.50× faster |
| deep: 768 files, 97 dirs | all files | 3.26 | 5.05 | 3.36 | 1.55× faster | 4.04 | 6.22 | 3.58 | 1.54× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.08 | 3.68 | 3.17 | 1.19× faster | 3.73 | 4.70 | 3.37 | 1.26× faster |
| mixed: 1,452 files, 364 dirs | all files | 3.94 | 5.40 | 5.68 | 1.37× faster | 2.75 | 4.88 | 5.93 | 1.77× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 3.82 | 5.23 | 5.53 | 1.37× faster | 2.63 | 4.85 | 5.74 | 1.85× faster |

Median wall time of 21 warm runs, process startup included. fynd at 672115b; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V45 96-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
