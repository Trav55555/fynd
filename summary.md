| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 11.64 | 25.01 | 19.46 | 2.15× faster | 11.54 | 16.94 | 20.48 | 1.47× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 11.21 | 21.02 | 18.81 | 1.87× faster | 11.43 | 16.04 | 20.02 | 1.40× faster |
| deep: 768 files, 97 dirs | all files | 4.19 | 6.50 | 4.31 | 1.55× faster | 5.37 | 7.60 | 4.35 | 1.41× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.92 | 4.66 | 4.08 | 1.19× faster | 5.12 | 6.08 | 4.14 | 1.19× faster |
| mixed: 1,452 files, 364 dirs | all files | 5.32 | 7.04 | 7.34 | 1.32× faster | 3.45 | 6.40 | 7.52 | 1.85× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 5.16 | 6.67 | 7.22 | 1.29× faster | 3.36 | 6.25 | 7.29 | 1.86× faster |

Median wall time of 21 warm runs, process startup included. fynd at ba0c38c; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
