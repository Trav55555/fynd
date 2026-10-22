| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 8.53 | 20.26 | 16.22 | 2.37× faster | 8.41 | 12.86 | 16.28 | 1.53× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 8.32 | 17.47 | 14.38 | 2.10× faster | 8.29 | 12.52 | 15.69 | 1.51× faster |
| deep: 768 files, 97 dirs | all files | 3.48 | 5.22 | 3.47 | 1.50× faster | 3.96 | 5.95 | 3.54 | 1.50× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.23 | 3.81 | 3.29 | 1.18× faster | 3.67 | 4.63 | 3.35 | 1.26× faster |
| mixed: 1,452 files, 364 dirs | all files | 4.05 | 5.54 | 5.78 | 1.37× faster | 2.68 | 5.28 | 5.79 | 1.97× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 3.91 | 5.49 | 5.60 | 1.40× faster | 2.56 | 4.95 | 5.71 | 1.93× faster |

Median wall time of 21 warm runs, process startup included. fynd at d51237f; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 9V45 96-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
