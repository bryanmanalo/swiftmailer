<?php
/**
 * @file
 * An administration page which allows for sending a test e-mail.
 */

function swiftmailer_admin_test_page() {
  return drupal_get_form('swiftmailer_admin_test_form');
}

function swiftmailer_admin_test_form($form, &$form_state) {

  $form['#tree'] = TRUE;

  $form['description'] = array(
    '#markup' => t('<p>This page allows you to send a test e-mail to a recipient of your choice.</p>'),
  );

  if (swiftmailer_validate_library(variable_get(SWIFTMAILER_VARIABLE_PATH, SWIFTMAILER_VARIABLE_PATH_DEFAULT))) {

    $form['test'] = array(
      '#type' => 'fieldset',
      '#title' => t('Recipient'),
      '#description' => t('<p>You can send a test e-mail to a recipient of your choice. The e-mail will be sent using the default values as provided by the Swift Mailer module or as configured by you.</p>'),
    );

    global $user;

    $form['test']['recipient'] = array(
      '#title' => t('E-mail'),
      '#type' => 'textfield',
      '#default_value' => check_plain($user->mail),
    );

    $form['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Send'),
    );

  } else {

    $form['message'] = array(
      '#markup' => t('<p>You need to configure the location of the Swift Mailer library. Please visit the !page
        and configure the library to enable the configuration options on this page.</p>',
      array('!page' => l(t('library configuration page'), 'admin/config/swiftmailer'))),
    );

  }

  return $form;
}

function swiftmailer_admin_test_form_submit($form, &$form_state) {

  if (isset($form_state['values']['test']['recipient'])) {
    drupal_mail('swiftmailer', 'test', $form_state['values']['test']['recipient'], language_default());
    drupal_set_message(t('An e-mail has been sent to !email.', array('!email' =>  check_plain($form_state['values']['test']['recipient']))));
  }

}