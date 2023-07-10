# For https://arthurchiao.art/blog/gpu-prices

def calc_discounts(title, discount_list, price_list):
    print(title)
    for price in price_list:
        items = []
        for discount in discount_list:
            items.append(price * discount)
        line = "| "
        for item in items:
            # line += "%7.1f | " % item
            line += "%7.3f | " % item
        print(line)
    print("")


discounts = [0.7, 0.6, 0.5, 0.4, 0.3]

############################# AlibabaCloud ####################################
# A100
# prices1 = [34.7, 31.5, 138.9, 277.9, 126.3, 252.6, 277.9]
# calc_discounts("A100 pay_as_you_go_prices", discounts, prices1)
# prices2 = [16676.0, 15160.0, 66704.0, 133408.0, 60640.0, 121280.0, 133408.0]
# calc_discounts("A100 subscription_prices", discounts, prices2)

# V100
# prices1 = [26.46, 105.84, 211.68, 219.64, 19.73, 78.95, 237.12, 157.91]
# calc_discounts("V100 pay_as_you_go_prices", discounts, prices1)
# prices2 = [7620.0, 30480.0, 60960.0, 63255.0, 9475.0, 37900.0, 68292.0, 75800.0]
# calc_discounts("V100 subscription_prices", discounts, prices2)

# T4
# prices1 = [11.63, 14.00, 16.41, 17.19, 14.81, 34.38, 68.75, 68.75]
# calc_discounts("T4 pay_as_you_go_prices", discounts, prices1)
# prices2 = [3348.0, 4032.0, 4725.0, 4950.0 , 7112.9 , 9900.0 ,19800.0 ,19800.0]
# calc_discounts("T4 subscription_prices", discounts, prices2)

# A10
prices1 = [9.53, 10.09, 13.30, 17.94, 21.53, 26.61, 53.23]
calc_discounts("A10 pay_as_you_go_prices", discounts, prices1)
prices2 = [4575.66, 4844.81, 6387.98, 8613.00, 10335.60, 12775.95, 25551.90]
calc_discounts("A10 subscription_prices", discounts, prices2)
############################# AlibabaCloud ####################################

############################# AWS #############################################
# T4, frankfurt
# prices1 = [0.658, 1.308, 1.505, 4.192, 5.015, 5.440, 9.780]
# calc_discounts("T4 on_demand_prices", discounts, prices1)
# V100, frankfurt
# prices1 = [3.823, 15.292, 30.584]
# calc_discounts("V100 on_demand_prices", discounts, prices1)

# T4, singapore
# prices1 = [0.736, 1.052, 1.685, 3.045, 5.474, 6.089, 10.948]
# calc_discounts("T4 on_demand_prices", discounts, prices1)

# V100, singapore
# prices1 = [4.234, 16.936, 33.872]
# calc_discounts("V100 on_demand_prices", discounts, prices1)
############################# AWS #############################################
