// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
#include <iostream>
#include <math.h>
#include <stdio.h>
#include <vector>
#include <numeric>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "platform.h"
#include "polygon.h"
#include "net.h"


void PseAdaptor(ncnn::Mat& features,
                              std::map<int, std::vector<cv::Point>>& contours_map,
                              const float thresh,
                              const float min_area,
                              const float ratio) 
{

        /// get kernels
        float *srcdata = (float *) features.data;
        std::vector<cv::Mat> kernels;
          
        float _thresh = thresh;
        cv::Mat scores = cv::Mat::zeros(features.h, features.w, CV_32FC1);
        for (int c = features.c - 1; c >= 0; --c){
            cv::Mat kernel(features.h, features.w, CV_8UC1);
            for (int i = 0; i < features.h; i++) {
                for (int j = 0; j < features.w; j++) {
               
                    if (c==features.c - 1) scores.at<float>(i, j) = srcdata[i * features.w + j + features.w*features.h*c ] ;

                    if (srcdata[i * features.w + j + features.w*features.h*c ] >= _thresh) {
                    // std::cout << srcdata[i * src.w + j] << std::endl;
                        kernel.at<uint8_t>(i, j) = 1;
                    } else {
                        kernel.at<uint8_t>(i, j) = 0;
                        }
               
                }
            }
            kernels.push_back(kernel);
            _thresh = thresh * ratio;
        }

    
        /// make label
        cv::Mat label;
        std::map<int, int> areas;
        std::map<int, float> scores_sum;
        cv::Mat mask(features.h, features.w, CV_32S, cv::Scalar(0));
        cv::connectedComponents(kernels[features.c  - 1], label, 4);


       

        for (int y = 0; y < label.rows; ++y) {
            for (int x = 0; x < label.cols; ++x) {
                int value = label.at<int32_t>(y, x);
                float score = scores.at<float>(y,x);
                if (value == 0) continue;
                areas[value] += 1;
                scores_sum[value] += score;
            }
        }

        std::queue<cv::Point> queue, next_queue;

        for (int y = 0; y < label.rows; ++y) {
            
            for (int x = 0; x < label.cols; ++x) {
                int value = label.at<int>(y, x);

                if (value == 0) continue;
                if (areas[value] < min_area) {
                    areas.erase(value);
                    continue;
                }
               
                if (scores_sum[value]*1.0 /areas[value] < 0.93  )
                {
                    areas.erase(value);
                    scores_sum.erase(value);
                    continue;
                }
                cv::Point point(x, y);
                queue.push(point);
                mask.at<int32_t>(y, x) = value;
            }
        }

        /// growing text line
        int dx[] = {-1, 1, 0, 0};
        int dy[] = {0, 0, -1, 1};

        for (int idx = features.c  - 2; idx >= 0; --idx) {
            while (!queue.empty()) {
                cv::Point point = queue.front(); queue.pop();
                int x = point.x;
                int y = point.y;
                int value = mask.at<int32_t>(y, x);

                bool is_edge = true;
                for (int d = 0; d < 4; ++d) {
                    int _x = x + dx[d];
                    int _y = y + dy[d];

                    if (_y < 0 || _y >= mask.rows) continue;
                    if (_x < 0 || _x >= mask.cols) continue;
                    if (kernels[idx].at<uint8_t>(_y, _x) == 0) continue;
                    if (mask.at<int32_t>(_y, _x) > 0) continue;

                    cv::Point point_dxy(_x, _y);
                    queue.push(point_dxy);

                    mask.at<int32_t>(_y, _x) = value;
                    is_edge = false;
                }

                if (is_edge) next_queue.push(point);
            }
            std::swap(queue, next_queue);
        }

        /// make contoursMap
        for (int y=0; y < mask.rows; ++y)
            for (int x=0; x < mask.cols; ++x) {
                int idx = mask.at<int32_t>(y, x);
                if (idx == 0) continue;
                contours_map[idx].emplace_back(cv::Point(x, y));
            }
}






cv::Mat resize_img(cv::Mat src,const int long_size)
{
    int w = src.cols;
    int h = src.rows;
    std::cout<<"原图尺寸 (" << w << ", "<<h<<")"<<std::endl;
    float scale = 1.f;
    if (w > h)
    {
        scale = (float)long_size / w;
        w = long_size;
        h = h * scale;
    }
    else
    {
        scale = (float)long_size / h;
        h = long_size;
        w = w * scale;
    }
    if (h % 32 != 0)
    {
        h = (h / 32 + 1) * 32;
    }
    if (w % 32 != 0)
    {
        w = (w / 32 + 1) * 32;
    }
    std::cout<<"缩放尺寸 (" << w << ", "<<h<<")"<<std::endl;
    cv::Mat result;
    cv::resize(src, result, cv::Size(w, h));
    return result;
}

cv::Mat draw_bbox(cv::Mat &src, const std::vector<std::vector<cv::Point>> &bboxs) {
    cv::Mat dst;
    if (src.channels() == 1) {
        cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
    } else {
        dst = src.clone();
    }
    auto color = cv::Scalar(0, 0, 255);
    for (auto bbox :bboxs) {

        cv::line(dst, bbox[0], bbox[1], color, 3);
        cv::line(dst, bbox[1], bbox[2], color, 3);
        cv::line(dst, bbox[2], bbox[3], color, 3);
        cv::line(dst, bbox[3], bbox[0], color, 3);
    }
    return dst;
}

static int detect_psenet(const char *model, const char *model_param, const char *imagepath, const int long_size = 800) {
    cv::Mat im_bgr = cv::imread(imagepath, 1);

    if (im_bgr.empty()) {
        fprintf(stderr, "cv::imread %s failed\n", imagepath);
        return -1;
    }
    // 图像缩放
    auto im = resize_img(im_bgr, long_size);
    float h_scale = im_bgr.rows * 1.0 / im.rows;
    float w_scale = im_bgr.cols * 1.0 / im.cols;

    ncnn::Mat in = ncnn::Mat::from_pixels(im.data, ncnn::Mat::PIXEL_BGR2RGB, im.cols, im.rows);
    const float mean_vals[3] = { 0.485 * 255, 0.456 * 255, 0.406 * 255};
    const float norm_vals[3] = { 1.0 / 0.229 / 255.0, 1.0 / 0.224 / 255.0, 1.0 /0.225 / 255.0};
    in.substract_mean_normalize(mean_vals,norm_vals);

    std::cout << "输入尺寸 (" << in.w << ", " << in.h << ")" << std::endl;

    ncnn::Net psenet;
    psenet.load_param(model_param);
    psenet.load_model(model);
    ncnn::Extractor ex = psenet.create_extractor();
    ex.set_num_threads(4);
    ex.input("input", in);

    ncnn::Mat preds;
    double time1 = static_cast<double>( cv::getTickCount());
    ex.extract("out", preds);
    std::cout << "前向时间:" << (static_cast<double>( cv::getTickCount()) - time1) / cv::getTickFrequency() << "s" << std::endl;
    std::cout << "网络输出尺寸 (" << preds.w << ", " << preds.h << ", " << preds.c << ")" << std::endl;

    time1 = static_cast<double>( cv::getTickCount());
    // cv::Mat score = cv::Mat::zeros(preds.h, preds.w, CV_32FC1);
    // cv::Mat thre = cv::Mat::zeros(preds.h, preds.w, CV_8UC1);
    // ncnn2cv(preds, score, thre);
    // auto bboxs = deocde(score, thre, 1, h_scale, w_scale);
    std::map<int, std::vector<cv::Point>> contoursMap;
    PseAdaptor(preds, contoursMap, 0.7311, 10, 1);
    std::vector<std::vector<cv::Point>> bboxs;
    for (auto &cnt: contoursMap) {
        cv::Mat bbox;
        cv::RotatedRect minRect = cv::minAreaRect(cnt.second);
        cv::boxPoints(minRect, bbox);
        std::vector<cv::Point> points;
        for (int i = 0; i < bbox.rows; ++i) {
                points.emplace_back(cv::Point(int(bbox.at<float>(i, 0) * w_scale), int(bbox.at<float>(i, 1) * h_scale)));
            }
        bboxs.emplace_back(points);
      
    }

    
    std::cout << "decode 时间:" << (static_cast<double>( cv::getTickCount()) - time1) / cv::getTickFrequency() << "s" << std::endl;
    std::cout << "boxzie" << bboxs.size() << std::endl;
    auto result = draw_bbox(im_bgr, bboxs);
    cv::imwrite("./imgs/result.jpg", result);
 
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s [model model path imagepath long_size]\n", argv[0]);
        return -1;
    }
    const char *model = argv[1];
    const char *model_param = argv[2];
    const char *imagepath = argv[3];
    const int long_size = atoi(argv[4]);
    std::cout << model << " " << model_param << " " << imagepath << " " << long_size << std::endl;

    detect_psenet(model, model_param, imagepath, long_size);
    return 0;
}
