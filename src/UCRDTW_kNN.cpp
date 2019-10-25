//
// Created by Qitong Wang on 2019/10/22.
// Modified from UCR_SUITE
//

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <iostream>
#include <queue>
#include "bounds.h"
#include "distances.h"
#include "parameters.h"
#include "query.h"
#include "sequence.h"
#include "thread_pool.h"

using namespace std;

struct Hit {
    Hit(long location, double distance) : location(location), distance(distance) {}

    long location;
    double distance;
};

struct compare {
    bool operator()(const Hit &a, const Hit &b) {
        return a.distance > b.distance;
    }
};

void cumulate(double *cumulated, double *original, int length) {
    cumulated[length - 1] = original[length - 1];
    for (int i = length - 2; i >= 0; --i) {
        cumulated[i] = cumulated[i + 1] + original[i];
    }
}

void conduct_query(const Sequence *sequence, const Query *query, const Parameters *parameters) {
    bool to_skip = false;
    int i, num_to_skip = -1, next = 0, pruned_by_kim = 0, pruned_by_keogh = 0, pruned_by_keogh_2 = 0;
    int epoch_size, num_epoches = 0, to_calculate, start, position_in_epoch, location;
    int overlap = (int) ((float) query->length * parameters->overlap_ratio);
    double sum, squared_sum, mean, std, bsf = INF, distance = 0, lb_kim = 0, lb_keogh_1 = 0, lb_keogh_2 = 0;
    priority_queue<Hit, vector<Hit>, compare> bsf_pq;

    auto buffer = (double *) malloc(sizeof(double) * parameters->epoch);
    auto upper_envelop = (double *) malloc(sizeof(double) * parameters->epoch);
    auto lower_envelop = (double *) malloc(sizeof(double) * parameters->epoch);
    auto current = (double *) malloc(sizeof(double) * query->length * 2);
    auto current_normalized = (double *) malloc(sizeof(double) * query->length);
    auto cumulative_bounds_keogh = (double *) malloc(sizeof(double) * query->length);
    auto bounds_keogh_1 = (double *) malloc(sizeof(double) * query->length);
    auto bounds_keogh_2 = (double *) malloc(sizeof(double) * query->length);
    if (buffer == nullptr || upper_envelop == nullptr || lower_envelop == nullptr || current == nullptr ||
        current_normalized == nullptr || cumulative_bounds_keogh == nullptr || bounds_keogh_1 == nullptr ||
        bounds_keogh_2 == nullptr) {
        error(1);
    }

    for (i = 0; i < query->length; ++i) {
        cumulative_bounds_keogh[i] = 0, bounds_keogh_1[i] = 0, bounds_keogh_2[i] = 0;
    }

    // Cumulative values (sum, squared_sum, etc) will be restarted to reduce floating point errors for every EPOCH points
    while (next + query->length <= sequence->length) {
        if (next + parameters->epoch > sequence->length) {
            copy(sequence->points + next, sequence->points + sequence->length, buffer);
            next = sequence->length;
            epoch_size = sequence->length - next;
        } else {
            copy(sequence->points + next, sequence->points + next + parameters->epoch, buffer);
            next += (parameters->epoch - query->length + 1);
            epoch_size = parameters->epoch;
        }

        lower_upper_lemire(buffer, epoch_size, parameters->warping_window, lower_envelop, upper_envelop);

        sum = 0, squared_sum = 0;
        for (to_calculate = 0; to_calculate < epoch_size; to_calculate++) {
            current[to_calculate % query->length] = buffer[to_calculate];
            current[(to_calculate % query->length) + query->length] = buffer[to_calculate];

            sum += buffer[to_calculate];
            squared_sum += (buffer[to_calculate] * buffer[to_calculate]);

            if (to_calculate < query->length - 1) {
                continue;
            }

            start = (to_calculate + 1) % query->length;

            // TODO skip the next following overlap sub-sequences to check is an easy but not optimal implementation
            if (to_skip) {
                num_to_skip -= 1;
                if (num_to_skip <= 0) {
                    to_skip = false;
                }
                goto UPDATE_STATISTICS;
            }

            mean = sum / query->length;
            std = sqrt(squared_sum / query->length - mean * mean);
            position_in_epoch = to_calculate - (query->length - 1);

            lb_kim = lb_kim_hierarchy(current, query->normalized_points, start, query->length, mean, std, bsf);

            if (lb_kim >= bsf) {
                pruned_by_kim += 1;
                goto UPDATE_STATISTICS;
            }

            // z_normalization of t will be computed on the fly.
            lb_keogh_1 = lb_keogh_cumulative(query->sorted_indexes, current, query->sorted_upper_envelop,
                                             query->sorted_lower_envelop, bounds_keogh_1, start, query->length, mean,
                                             std,
                                             bsf);
            if (lb_keogh_1 >= bsf) {
                pruned_by_keogh += 1;
                goto UPDATE_STATISTICS;
            }

            // TODO Note that for better optimization, this can merge to the previous lb_keogh_cumulative
            for (i = 0; i < query->length; ++i) {
                current_normalized[i] = (current[(i + start)] - mean) / std;
            }

            lb_keogh_2 = lb_keogh_data_cumulative(query->sorted_indexes, current_normalized,
                                                  query->sorted_normalized_points, bounds_keogh_2,
                                                  lower_envelop + position_in_epoch, upper_envelop + position_in_epoch,
                                                  query->length, mean, std, bsf);
            if (lb_keogh_2 >= bsf) {
                pruned_by_keogh_2 += 1;
                goto UPDATE_STATISTICS;
            }

            cumulate(cumulative_bounds_keogh, lb_keogh_1 > lb_keogh_2 ? bounds_keogh_1 : bounds_keogh_2, query->length);

            distance = dtw(current_normalized, query->normalized_points, cumulative_bounds_keogh, query->length,
                       parameters->warping_window, bsf);

            if (distance < bsf) {
                location = num_epoches * (parameters->epoch - query->length + 1) + to_calculate - query->length + 1;
                bsf_pq.push(Hit(location, distance));
                if (bsf_pq.size() > parameters->num_neighbors) {
                    bsf_pq.pop();
                }
                bsf = bsf_pq.top().distance;
                to_skip = true;
                num_to_skip = overlap;
            }

            UPDATE_STATISTICS:
            sum -= current[start];
            squared_sum -= (current[start] * current[start]);
        }
        num_epoches += 1;
    }

    free(buffer);
    free(upper_envelop);
    free(lower_envelop);
    free(current);
    free(current_normalized);
    free(cumulative_bounds_keogh);
    free(bounds_keogh_1);
    free(bounds_keogh_2);

    for (i = 0; i < parameters->num_neighbors; ++i) {
        cout << "Location : " << bsf_pq.top().location << endl;
        cout << "Distance : " << sqrt(bsf_pq.top().distance) << endl;
        bsf_pq.pop();
    }
}

int main(int argc, char *argv[]) {
    double start_time = clock();

    Parameters parameters(argc, argv);
    FILE *database_fp = fopen(parameters.database_filename.c_str(), "r");
    FILE *queries_fp = fopen(parameters.queries_filename.c_str(), "r");
    if (database_fp == nullptr || queries_fp == nullptr) {
        error(2);
    }

    int sequence_length = 1000000;
    if (sequence_length < parameters.query_length) {
        cout << "queried sequence not long enough" << endl;
        exit(1);
    }

    Sequence sequence(database_fp, sequence_length);
    Query query(queries_fp, parameters.query_length, parameters.warping_window);
    // TODO reserved for multiple queries
    fclose(queries_fp);
    fclose(database_fp);

    ThreadPool threadPool;
    threadPool.enqueue(boost::bind(conduct_query, &sequence, &query, &parameters));

    cout << "Total Execution Time: " << (clock() - start_time) / CLOCKS_PER_SEC << " sec" << endl;
    return 0;
}
