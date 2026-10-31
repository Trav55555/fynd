| Tree | Query | One thread: fynd (ms) | fd (ms) | find (ms) | fynd vs fd | Default threads: fynd (ms) | fd (ms) | find (ms) | fynd vs fd |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| wide: 20,000 files, 1 dir | all files | 11.10 | 25.83 | 19.26 | 2.33× faster | 10.80 | 16.43 | 19.88 | 1.52× faster |
| wide: 20,000 files, 1 dir | name has 'needle' (1,819) | 10.64 | 21.70 | 18.88 | 2.04× faster | 10.73 | 15.60 | 18.82 | 1.45× faster |
| deep: 768 files, 97 dirs | all files | 4.13 | 7.21 | 4.39 | 1.75× faster | 5.35 | 8.27 | 4.36 | 1.54× faster |
| deep: 768 files, 97 dirs | name has 'needle' (96) | 3.92 | 4.77 | 4.14 | 1.22× faster | 5.20 | 6.25 | 4.18 | 1.20× faster |
| mixed: 1,452 files, 364 dirs | all files | 4.96 | 7.04 | 7.27 | 1.42× faster | 3.52 | 5.99 | 7.22 | 1.70× faster |
| mixed: 1,452 files, 364 dirs | name has 'needle' (242) | 4.89 | 6.87 | 7.08 | 1.40× faster | 3.39 | 5.75 | 7.03 | 1.70× faster |

Median wall time of 21 warm runs, process startup included. fynd at cac89e4; fdfind 9.0.0; find (GNU findutils) 4.9.0. One thread: fynd -j1, fd --threads 1. Default threads: no thread option, so fynd uses the online CPU count and fd its own automatic count. GitHub-hosted ubuntu-24.04 runner, 4 CPUs, AMD EPYC 7763 64-Core Processor. Shared CI runners are noisy: compare ratios, not absolute times.
