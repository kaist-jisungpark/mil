#!/usr/bin/env python
import cv2
import rospy
import numpy as np

__author__ = "Kevin Allen"


class Threshold(object):
    def __init__(self, low, high, conversion_code=None, in_space='BGR', thresh_space='BGR'):
        '''
        Convience class to store lower and upper bounds for doing a color threshold in OpenCV

        See examples for clearer understanding of parameters.
        @param low A list represnting the lower acceptable values for thresholding an image
        @param high A list representing the high acceptable values for thresholding an image
        @param conversion_code When not None, passed to cv2.cvtColor to change colorspace before thresholding image
        @param in_space All caps string that's a valid OpenCV colorspace ('HSV', 'BGR', 'LAB', etc),
                        used to determine conversion_code if conversion_code is None
        @param thresh_space All caps string that's a valid OpenCV colorspace, used to convert
                            image before thresholding. Used to determine conversion_code if it's None

        ex:
        Threshold((0, 0, 0), (255, 255, 255)) # Thresholds BGR image in BGR colorpace
        Threshold([25, 180 180], np.array([50, 190, 200]), thresh_space='LAB')  # Thresholds BGR image in LAB
        Threshold((50, 50, 50), [200, 200, 200], in_space='HSV', thresh_space='LAB') # Threshold HSV image in LAB
        '''
        assert isinstance(low, (tuple, list, np.ndarray)), 'param lower must be a tuple/list/np.ndarray'
        assert isinstance(high, (tuple, list, np.ndarray)), 'param upper must be a tuple/list/np.ndarray'
        self.low = np.array(low)
        self.high = np.array(high)
        self.in_space = in_space
        self.thresh_space = thresh_space
        # If conversion code not specified, try to form it from other params
        if conversion_code is None and in_space != thresh_space:
            try:
                self.conversion_code = getattr(cv2, 'COLOR_{}2{}'.format(in_space, thresh_space))
            except AttributeError:
                raise Exception('Could not determine conversion code from params.\
                                 Are [{}, {}] valid OpenCV colorspaces?'.format(in_space, thresh_space))
        else:
            self.conversion_code = conversion_code

    @classmethod
    def from_dict(cls, d, in_space='BGR', thresh_space=None):
        '''
        Loads thresholds from a dictionary. See examples for valid dictionaries.

        @param in_space See __init__ for valid values.
        @param thresh_space If None, loads threshold in colorspace of first key in dictionary.
                            Otherwise, see __init__ for valid values
        Valid Examples:

        { 'HSV':
            'low': [0, 20, 50],
            'high': [255, 255, 255] }
        -> Threshold([0, 20, 50], [255, 255, 255], thresh_space='HSV')

        { 'LAB': [0, 66, 66], [255, 180, 180] }
        -> Threshold([0, 66, 66], [255, 180, 180], thresh_space='LAB')
        '''
        assert isinstance(d, dict), 'd is not a dictionary'
        if thresh_space is None:
            assert len(d) > 0, 'Dictionary is empty'
            return cls.from_dict(d, in_space=in_space, thresh_space=d.keys()[0])
        assert thresh_space in d, '{} color space not in dictionary'.format(thresh_space)
        inner = d[thresh_space]
        if 'low' in inner and 'high' in inner:
            return cls(inner['low'], inner['high'], in_space=in_space, thresh_space=thresh_space)
        assert len(inner) == 2, 'Cannot get low and high bounds from dictionary'
        return cls(inner[0], inner[1], in_space=in_space, thresh_space=thresh_space)

    @classmethod
    def from_param(cls, param, in_space='BGR', thresh_space=None):
        '''
        Loads thresholds from a ROS param. Param must be a valid dictionary, see from_dict
        '''
        return cls.from_dict(rospy.get_param(param), in_space=in_space, thresh_space=thresh_space)

    def threshold(self, img):
        if self.conversion_code is not None:
            converted = cv2.cvtColor(img, self.conversion_code)
            return cv2.inRange(converted, self.low, self.high)
        return cv2.inRange(img, self.low, self.high)

    __call__ = threshold  # Calling this will threshold an image

    def create_trackbars(self, window=None):
        '''
        Create OpenCV GUI trackbars to adjust the threshold values live.

        @param window Name of OpenCV window to put trackbars in
        '''
        if window is None:
            window = 'threshold'
            cv2.namedWindow(window)

        def set_thresh(t, i, x):
            if t == 'low':
                self.low[i] = x
            if t == 'high':
                self.high[i] = x
        for i in range(len(self.low)):
            cv2.createTrackbar('low {}'.format(i), window, int(self.low[i]), 255,
                               lambda x, _self=self, _i=i: set_thresh('low', _i, x))
        for i in range(len(self.high)):
            cv2.createTrackbar('high {}'.format(i), window, int(self.high[i]), 255,
                               lambda x, _self=self, _i=i: set_thresh('high', _i, x))

    def __str__(self):
        if self.conversion_code is not None:
            return 'Threshold from {} to {} using conversion code {}'.format(self.low, self.high, self.conversion_code)
        return 'Threshold {} images in {} colorspace from {} to {}'.format(
            self.in_space, self.thresh_space, self.low, self.high)

    def __repr__(self):
        return str((self.low, self.high, self.conversion_code))
