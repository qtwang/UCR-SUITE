//
// Created by Qitong Wang on 2019/10/24.
//

#ifndef UCR_SUITE_QUERY_H
#define UCR_SUITE_QUERY_H

#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>

#include "utils.h"
#include "bounds.h"

class Query {
public:
    int length, overlap_length, warping_window;
    int *sorted_indexes;
    double *normalized_values, *sorted_normalized_values, *sorted_upper_envelop, *sorted_lower_envelop;

    Query(ifstream &query_ifs, int length, int warping_window, int overlap_length);

    ~Query();

private:
    static void sort_indices(const double *values, int *indexes, int length);
};

Query::Query(ifstream &query_ifs, int length, int warping_window, int overlap_length) : length(length),
                                                                                        warping_window(warping_window),
                                                                                        overlap_length(overlap_length) {
    auto values = (double *) malloc(sizeof(double) * length);
    auto upper_envelop = (double *) malloc(sizeof(double) * length);
    auto lower_envelop = (double *) malloc(sizeof(double) * length);

    this->sorted_indexes = (int *) malloc(sizeof(int) * length);
    this->normalized_values = (double *) malloc(sizeof(double) * length);
    this->sorted_normalized_values = (double *) malloc(sizeof(double) * length);
    this->sorted_upper_envelop = (double *) malloc(sizeof(double) * length);
    this->sorted_lower_envelop = (double *) malloc(sizeof(double) * length);

    if (this->sorted_indexes == nullptr || values == nullptr || this->normalized_values == nullptr ||
        upper_envelop == nullptr || lower_envelop == nullptr || this->sorted_normalized_values == nullptr ||
        this->sorted_upper_envelop == nullptr || this->sorted_lower_envelop == nullptr) {
        error(1);
    }

    double sum = 0, squared_sum = 0, mean, std;

    for (int i = 0; i < length; ++i) {
        query_ifs >> values[i];
        sum += values[i];
        squared_sum += (values[i] * values[i]);
    }

    mean = sum / length;
    std = sqrt(squared_sum / length - mean * mean);
    for (int i = 0; i < length; ++i) {
        this->normalized_values[i] = (values[i] - mean) / std;
    }

    get_envelops_lemire(this->normalized_values, length, warping_window, lower_envelop, upper_envelop);
    sort_indices(this->normalized_values, this->sorted_indexes, length);

    for (int i = 0; i < length; ++i) {
        this->sorted_normalized_values[i] = this->normalized_values[this->sorted_indexes[i]];
        this->sorted_upper_envelop[i] = upper_envelop[this->sorted_indexes[i]];
        this->sorted_lower_envelop[i] = lower_envelop[this->sorted_indexes[i]];
    }

    free(values);
    free(upper_envelop);
    free(lower_envelop);
}

Query::~Query() {
    free(this->sorted_indexes);
    free(this->normalized_values);
    free(this->sorted_normalized_values);
    free(this->sorted_upper_envelop);
    free(this->sorted_lower_envelop);
}

void Query::sort_indices(const double *values, int *indexes, int length) {
    iota(indexes, indexes + length, 0);
    sort(indexes, indexes + length, [values](int i1, int i2) { return abs(values[i1]) > abs(values[i2]); });
}

#endif //UCR_SUITE_QUERY_H
